#include "./log.hpp"

#include <assert.h>  //assert
#include <errno.h>
#include <stdarg.h>       //va_list
#include <sys/stat.h>     //mkdir
#include <sys/syscall.h>  //system call
#include <unistd.h>       //access, getpid

#define MEM_USE_LIMIT (3u * 1024 * 1024 * 1024)  // 3GB, 所有日志加起来最多MEM_USE_LIMIT字节
#define LOG_USE_LIMIT (1u * 1024 * 1024 * 1024)  // 1GB, 一个日志文件最多LOG_USE_LIMIT字节
#define LOG_LEN_LIMIT (4 * 1024)                 // 4K, 一行日志最多LOG_LEN_LIMIT字节
#define RELOG_THRESOLD 5
#define BUFF_WAIT_TIME 1

pid_t gettid() {
    return syscall(__NR_gettid);
}

pthread_mutex_t ring_log::_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t ring_log::_cond = PTHREAD_COND_INITIALIZER;

ring_log* ring_log::_ins = NULL;
pthread_once_t ring_log::_once = PTHREAD_ONCE_INIT;
uint32_t ring_log::_one_buff_len = 30 * 1024 * 1024;  // 30MB

/* 初始化，以及建立双向链表 */
ring_log::ring_log()
    : _buff_cnt(3),
      _curr_buf(NULL),
      _prst_buf(NULL),
      _fp(NULL),
      _log_cnt(0),
      _env_ok(false),
      _level(INFO),
      _lst_lts(0),
      _tm() {
    /* 创建双向循环链表 */
    cell_buffer* head = new cell_buffer(_one_buff_len);
    if (!head) {
        fprintf(stderr, "no space to allocate cell_buffer\n");
        exit(1);
    }
    cell_buffer* current;
    cell_buffer* prev = head;
    for (int i = 1; i < _buff_cnt; ++i) {
        current = new cell_buffer(_one_buff_len);
        if (!current) {
            fprintf(stderr, "no space to allocate cell_buffer\n");
            exit(1);
        }
        current->prev = prev;
        prev->next = current;
        prev = current;
    }
    prev->next = head;
    head->prev = prev;

    _curr_buf = head;
    _prst_buf = head;

    /* cell buffer双线链表创建完毕之后，获取当前进程的id */
    _pid = getpid();
}

void ring_log::init_path(const char* log_dir,
                         const char* prog_name,
                         int level) {
    pthread_mutex_lock(&_mutex);

    /* 拷贝 log 文件夹路径和程序名 */
    strncpy(_log_dir, log_dir, 512);
    /* _prog_name的格式:  name_year-mon-day-t[tid].log.n */
    strncpy(_prog_name, prog_name, 128);

    /* 创建存储的log文件的文件夹 */
    mkdir(_log_dir, 0777);

    /* 查看是否存在此目录、目录下是否允许创建文件 */
    if (access(_log_dir, F_OK | W_OK) == -1) {
        fprintf(stderr, "logdir: %s error: %s\n", _log_dir, strerror(errno));
    } else {
        _env_ok = true;
    }

    /* 检查并设置记录的级别，如果超出了允许的范围，则设为最高或最低级别 */
    if (level > TRACE)
        level = TRACE;
    if (level < FATAL)
        level = FATAL;
    _level = level;

    pthread_mutex_unlock(&_mutex);
}

/**
 * @brief 持久化，从文件中向缓冲区中追加内容到文件(磁盘)，该函数是给消费者使用的。
 * 信号量超时时间为1s，超过一秒即便该缓冲区仍然未满：消费者将此cell_buffer标记为FULL强行持久化
 */
void ring_log::persist() {
    while (true) {
        /* 检查 _prst_buf 是否需要持久化 */
        pthread_mutex_lock(&_mutex);
        if (_prst_buf->status == cell_buffer::FREE) {
            /* 如果持久化缓存仍然可追加日志，那就阻塞当前线程 */
            struct timespec tsp;
            struct timeval now;
            gettimeofday(&now, NULL);
            tsp.tv_sec = now.tv_sec;
            tsp.tv_nsec = now.tv_usec * 1000;  
            tsp.tv_sec += BUFF_WAIT_TIME;       /* 设置超时事件为1s */
            /* 超过一秒即便该缓冲区仍然未满：也要解除阻塞 */
            pthread_cond_timedwait(&_cond, &_mutex, &tsp);
        }

        if (_prst_buf->empty()) {
            /* 如果持久化内存为空，则放弃，进入下一个循环 */
            pthread_mutex_unlock(&_mutex);
            continue;
        }

        if (_prst_buf->status == cell_buffer::FREE) {
            /* 接触信号量阻塞之后再次检测，如果缓存依旧可追加，说明当前生产者还在该buffer */
            assert(_curr_buf == _prst_buf); 
            /* 即便生产者仍然在向buffer写入，但是消费者也要强制持久化了 */
            _curr_buf->status = cell_buffer::FULL;
            /* 让生产者去下一个buffer生产 */
            _curr_buf = _curr_buf->next;
        }

        int year = _tm.year, mon = _tm.mon, day = _tm.day;
        pthread_mutex_unlock(&_mutex);

        // decision which file to write
        if (!decis_file(year, mon, day))
            continue;
        
        /* 持久化：将缓冲区内容写入到文件 */
        _prst_buf->persist(_fp);
        fflush(_fp);

        pthread_mutex_lock(&_mutex);
        _prst_buf->clear();
        _prst_buf = _prst_buf->next;
        pthread_mutex_unlock(&_mutex);
    }
}

/**
 * @brief 将一条日志加入到环形缓冲区中，如果当前缓冲区已满，则将日志写入下一个缓冲区
 * @param lvl：日志等级
 * @param format：日志格式
 * @param ...：变长参数列表，对应于日志格式字符串中的占位符
 */
void ring_log::try_append(const char* lvl, const char* format, ...) {
    int ms;
    uint64_t curr_sec = _tm.get_curr_time(&ms);

    /* 如果距离上一次日志写入的时间小于阈值 RELOG_THRESOLD，则不写入 */
    if (_lst_lts && curr_sec - _lst_lts < RELOG_THRESOLD)
        return;

    char log_line[LOG_LEN_LIMIT];
    /* 格式化时间戳，将等级和时间戳拼接到 log_line 中 */
    int prev_len = snprintf(log_line, LOG_LEN_LIMIT, "%s[%s.%03d]", lvl, _tm.utc_fmt, ms);

    va_list arg_ptr;
    va_start(arg_ptr, format);

    /* 格式化变长参数列表中的日志信息，并将结果拼接到 log_line 的后面 */
    int main_len = vsnprintf(log_line + prev_len, LOG_LEN_LIMIT - prev_len,
                             format, arg_ptr);

    va_end(arg_ptr);

    uint32_t len = prev_len + main_len;

    _lst_lts = 0;
    bool tell_back = false;

    pthread_mutex_lock(&_mutex);
    /* 如果当前缓冲区未满且足够写入当前数据，将日志写入当前缓冲区 */
    if (_curr_buf->status == cell_buffer::FREE && _curr_buf->avail_len() >= len) {
        _curr_buf->append(log_line, len);
    } else {
        /* 当前缓冲区已满或者不够写入当前数据，需要将日志写入下一个缓冲区 */

        if (_curr_buf->status == cell_buffer::FREE) {
            /* 如果当前缓冲区未满，但是不够写入len大小的数据了，要去下一个缓冲区 */
            _curr_buf->status = cell_buffer::FULL;  // set to FULL
            cell_buffer* next_buf = _curr_buf->next;
            /* 告诉后端线程，当前缓冲区已满，需要将其写入磁盘 */
            tell_back = true;

            // 如果下一个缓冲区也是 FULL 状态，则无法使用下一个缓冲区，需要新开一个缓冲区
            if (next_buf->status == cell_buffer::FULL) {
                // 如果内存使用量已达到上限MEM_USE_LIMIT，则无法新开缓冲区，只能将当前缓冲区设置为下一个缓冲区
                if (_one_buff_len * (_buff_cnt + 1) > MEM_USE_LIMIT) {
                    fprintf(stderr, "no more log space can use\n");
                    _curr_buf = next_buf;
                    _lst_lts = curr_sec;
                } else {
                    /* 创建一个新的缓存区，并加入双向链表 */
                    cell_buffer* new_buffer = new cell_buffer(_one_buff_len);
                    _buff_cnt += 1;
                    new_buffer->prev = _curr_buf;
                    _curr_buf->next = new_buffer;
                    new_buffer->next = next_buf;
                    next_buf->prev = new_buffer;
                    _curr_buf = new_buffer;
                }
            } else {
                /* 下一个缓冲区为free，可以直接进行写入 */
                _curr_buf = next_buf;
            }
            if (!_lst_lts) {
                /* 如果未达到MEM_USE_LIMIT，_lst_lts为0，可以直接追加日志 */
                _curr_buf->append(log_line, len);
            }
                
        } else  //_curr_buf->status == cell_buffer::FULL, assert persist is on here too!
        {
            /* 当前缓冲区已满，无法追加日志 */
            _lst_lts = curr_sec;
        }
    }
    pthread_mutex_unlock(&_mutex);
    if (tell_back) {
        /* 让消费者线程解除阻塞，开启持久化工作 */
        pthread_cond_signal(&_cond);
    }
}

/**
 * @brief 决定根据日志消息的时间写时间？还是自主写时间？默认自主写时间
 * @return 操作是否成功
 */
bool ring_log::decis_file(int year, int mon, int day) {
    if (!_env_ok) {
        /* 打开文件 */
        if (_fp)
            fclose(_fp);
        _fp = fopen("/dev/null", "w");
        return _fp != NULL;
    }

    if (!_fp) {
        /* 文件指针为空的话，创建一个文件并打开 */
        _year = year, _mon = mon, _day = day;
        char log_path[1024] = {};
        sprintf(log_path, "%s/%s.%d%02d%02d.%u.log", _log_dir, _prog_name,
                _year, _mon, _day, _pid);
        _fp = fopen(log_path, "w");
        if (_fp)
            _log_cnt += 1;
    } else if (_day != day) {
        /* 文件指针非空，先关闭再打开 */
        fclose(_fp);
        char log_path[1024] = {};
        _year = year, _mon = mon, _day = day;
        sprintf(log_path, "%s/%s.%d%02d%02d.%u.log", _log_dir, _prog_name,
                _year, _mon, _day, _pid);
        _fp = fopen(log_path, "w");
        if (_fp)
            _log_cnt = 1;
    } else if (ftell(_fp) >= LOG_USE_LIMIT) {
        /* _fp使用的字符长度已经超过的LOG_USE_LIMIT的话，重新开一个文件 */
        fclose(_fp);
        char old_path[1024] = {};
        char new_path[1024] = {};
        /* 更新log文件的名字，即.log为最新文件， mv xxx.log.[i] xxx.log.[i + 1] */
        for (int i = _log_cnt - 1; i > 0; --i) {
            sprintf(old_path, "%s/%s.%d%02d%02d.%u.log.%d", _log_dir,
                    _prog_name, _year, _mon, _day, _pid, i);
            sprintf(new_path, "%s/%s.%d%02d%02d.%u.log.%d", _log_dir,
                    _prog_name, _year, _mon, _day, _pid, i + 1);
            rename(old_path, new_path);
        }
        // mv xxx.log xxx.log.1
        sprintf(old_path, "%s/%s.%d%02d%02d.%u.log", _log_dir, _prog_name,
                _year, _mon, _day, _pid);
        sprintf(new_path, "%s/%s.%d%02d%02d.%u.log.1", _log_dir, _prog_name,
                _year, _mon, _day, _pid);
        rename(old_path, new_path);
        _fp = fopen(old_path, "w");
        if (_fp)
            _log_cnt += 1;
    }
    return _fp != NULL;
}

void* be_thdo(void* args) {
    ring_log::ins()->persist();
    return NULL;
}
