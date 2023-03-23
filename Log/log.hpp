#ifndef __RING_LOG_H__
#define __RING_LOG_H__

#include <stdio.h>
#include <unistd.h>//access, getpid
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>//getpid, gettid
#include <sys/syscall.h>//system call

/* 日志级别 */
enum LOG_LEVEL
{
    FATAL = 1,
    ERROR,
    WARN,
    INFO,
    DEBUG,
    TRACE,
};

// extern pid_t gettid();

/* 时间类 */
struct utc_timer
{
    utc_timer()
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        /* 获取当前时间的秒数，存储在 _sys_acc_sec 中 */
        _sys_acc_sec = tv.tv_sec;
        /* 获取当前时间的分钟数，存储在 _sys_acc_min 中 */
        _sys_acc_min = _sys_acc_sec / 60;
        struct tm cur_tm;
        /* 计算年、月、日、时、分和秒 */
        localtime_r((time_t*)&_sys_acc_sec, &cur_tm);
        year = cur_tm.tm_year + 1900;
        mon  = cur_tm.tm_mon + 1;
        day  = cur_tm.tm_mday;
        hour  = cur_tm.tm_hour;
        min  = cur_tm.tm_min;
        sec  = cur_tm.tm_sec;
        reset_utc_fmt();
    }

    // 获取当前时间的秒数，并更新年、月、日、时、分和秒
    uint64_t get_curr_time(int* p_msec = NULL)
    {
        struct timeval tv;
        // 获取当前时间的秒数和微秒数
        gettimeofday(&tv, NULL);
        if (p_msec)
            *p_msec = tv.tv_usec / 1000;
        // 如果不在同一秒
        if ((uint32_t)tv.tv_sec != _sys_acc_sec)
        {
            // 更新秒数和系统当前秒数
            sec = tv.tv_sec % 60;
            _sys_acc_sec = tv.tv_sec;
            // 如果不在同一分钟
            if (_sys_acc_sec / 60 != _sys_acc_min)
            {
                
                _sys_acc_min = _sys_acc_sec / 60;
                struct tm cur_tm;
                localtime_r((time_t*)&_sys_acc_sec, &cur_tm);
                year = cur_tm.tm_year + 1900;
                mon  = cur_tm.tm_mon + 1;
                day  = cur_tm.tm_mday;
                hour = cur_tm.tm_hour;
                min  = cur_tm.tm_min;
                // 重新格式化 UTC 时间
                reset_utc_fmt();
            }
            else
            {
                // 仅更新秒数的 UTC 时间格式
                reset_utc_fmt_sec();
            }
        }
        return tv.tv_sec;
    }

    // 年、月、日、时、分、秒和 UTC 时间格式
    int year, mon, day, hour, min, sec;
    char utc_fmt[20];

private:
    // 格式化 UTC 时间
    void reset_utc_fmt()
    {
        snprintf(utc_fmt, 20, "%d-%02d-%02d %02d:%02d:%02d", year, mon, day, hour, min, sec);
    }
    
    // 格式化 UTC 时间中的秒数
    void reset_utc_fmt_sec()
    {
        snprintf(utc_fmt + 17, 3, "%02d", sec);
    }

    // 记录写上一条日志时，系统经过的时间
    uint64_t _sys_acc_min;
    uint64_t _sys_acc_sec;
};


class cell_buffer
{
public:
    /* cell buffer的两个状态 */
    enum buffer_status
    {
        FREE,   /* 表示当前缓冲区还有空间 */
        FULL    /* 表示暂时无法追加日志，正在、或即将被持久化到磁盘 */
    };

    /**
     * @brief cell_buffer初始化构造函数
     * @param len 缓冲区长度（缓冲区只存放字符）
     */
    cell_buffer(uint32_t len): 
    status(FREE), 
    prev(NULL), 
    next(NULL), 
    _total_len(len), 
    _used_len(0)
    {
        _data = new char[len];
        if (!_data)
        {
            fprintf(stderr, "no space to allocate _data\n");
            exit(1);
        }
    }

    /**
     * @brief 返回当前缓冲区可用的大小
     */
    uint32_t avail_len() const { return _total_len - _used_len; }


    /**
     * @brief 当前缓冲区是否为空
     */
    bool empty() const { return _used_len == 0; }

    /**
     * @brief 向缓冲区中追加内容
     * @param log_line 追加的字符串
     */
    void append(const char* log_line, uint32_t len)
    {
        if (avail_len() < len)
            return ;
        memcpy(_data + _used_len, log_line, len);
        _used_len += len;
    }

    /**
     * @brief 清空缓冲区
     */
    void clear()
    {
        /* 因为缓冲区的大小都是固定的，所以将_used_len置零即可 */
        _used_len = 0;
        status = FREE;
    }

    /**
     * @brief 持久化，从缓冲区中追加内容到文件(磁盘)
     * @param fp 文件指针
     */
    void persist(FILE* fp)
    {
        uint32_t wt_len = fwrite(_data, 1, _used_len, fp);
        if (wt_len != _used_len)
        {
            fprintf(stderr, "write log to disk error, wt_len %u\n", wt_len);
        }
        fflush(fp);
    }

    buffer_status status;

    cell_buffer* prev;  /* 指向前一个缓冲区 */
    cell_buffer* next;  /* 指向后一个缓冲区 */

private:
    /* 下面两个函数，禁止对象拷贝、赋值 */
    cell_buffer(const cell_buffer&);
    cell_buffer& operator=(const cell_buffer&);

    uint32_t _total_len;    /* 缓冲区长度 */
    uint32_t _used_len;     /* 缓冲区已经使用的长度 */
    char* _data;            /* 缓冲区 */
};

class ring_log
{
public:
    /**
     * @brief 用于线程安全的单例模式
     * @return 返回一个rain_log实例的指针
     */
    static ring_log* ins()
    {
        /* 本函数使用初值为PTHREAD_ONCE_INIT的once_control变量保证ring_log::init函数在本进程执行序列中仅执行一次。 */
        pthread_once(&_once, ring_log::init);
        return _ins;
    }

    /**
     * @brief 构建一个log实例
     */
    static void init()
    {
        while (!_ins) _ins = new ring_log();
    }

    void init_path(const char* log_dir, const char* prog_name, int level);

    int get_level() const { return _level; }

    void persist();

    void try_append(const char* lvl, const char* format, ...);

private:
    ring_log();

    bool decis_file(int year, int mon, int day);

    ring_log(const ring_log&);
    const ring_log& operator=(const ring_log&);

    int _buff_cnt;              /* buffer的个数——缓存区链表的长度 */

    cell_buffer* _curr_buf;     /* 生产者指针，指向当前缓存；该指针被多个生产者持有 */
    cell_buffer* _prst_buf;     /* 消费者指针，指向持久化缓存；该指针只能被一个消费者持有 */

    cell_buffer* last_buf;      

    FILE* _fp;                  /* 需要写入日志的文件的指针 */     
    pid_t _pid;                 /* 线程pid */ 
    int _year, _mon, _day;      
    int _log_cnt;               /* 日志文件计数器，从0开始 */  
    char _prog_name[128];       /* 程序名称 */
    char _log_dir[512];         

    bool _env_ok;               /* 当前日志的文件夹是否正常 */
    int _level;                 /* 日志级别 */
    uint64_t _lst_lts;          /* 代表上一次日志写入的时间戳(单位s)，如果该值非0，最后不能记录错误时间，记录错误发生在上次 */
    
    utc_timer _tm;

    static pthread_mutex_t _mutex;
    static pthread_cond_t _cond;

    static uint32_t _one_buff_len;  /* 一个缓冲区的大小 */

    //singleton
    static ring_log* _ins;
    static pthread_once_t _once;
};

void* be_thdo(void* args);

#define LOG_MEM_SET(mem_lmt) \
    do \
    { \
        if (mem_lmt < 90 * 1024 * 1024) \
        { \
            mem_lmt = 90 * 1024 * 1024; \
        } \
        else if (mem_lmt > 1024 * 1024 * 1024) \
        { \
            mem_lmt = 1024 * 1024 * 1024; \
        } \
        ring_log::_one_buff_len = mem_lmt; \
    } while (0)

#define LOG_INIT(log_dir, prog_name, level) \
    do \
    { \
        ring_log::ins()->init_path(log_dir, prog_name, level); \
        pthread_t tid; \
        pthread_create(&tid, NULL, be_thdo, NULL); \
        pthread_detach(tid); \
    } while (0)

//format: [LEVEL][yy-mm-dd h:m:s.ms][tid]file_name:line_no(func_name):content
#define LOG_TRACE(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= TRACE) \
        { \
            ring_log::ins()->try_append("[TRACE]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_DEBUG(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= DEBUG) \
        { \
            ring_log::ins()->try_append("[DEBUG]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_INFO(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= INFO) \
        { \
            ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_NORMAL(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= INFO) \
        { \
            ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_WARN(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= WARN) \
        { \
            ring_log::ins()->try_append("[WARN]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_ERROR(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= ERROR) \
        { \
            ring_log::ins()->try_append("[ERROR]", "[%u]%s:%d(%s): " fmt "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define LOG_FATAL(fmt, args...) \
    do \
    { \
        ring_log::ins()->try_append("[FATAL]", "[%u]%s:%d(%s): " fmt "\n", \
            gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
    } while (0)

#define TRACE(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= TRACE) \
        { \
            ring_log::ins()->try_append("[TRACE]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define DEBUG(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= DEBUG) \
        { \
            ring_log::ins()->try_append("[DEBUG]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define INFO(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= INFO) \
        { \
            ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define NORMAL(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= INFO) \
        { \
            ring_log::ins()->try_append("[INFO]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define WARN(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= WARN) \
        { \
            ring_log::ins()->try_append("[WARN]", "[%u]%s:%d(%s): " fmt "\n", \
                    gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define ERROR(fmt, args...) \
    do \
    { \
        if (ring_log::ins()->get_level() >= ERROR) \
        { \
            ring_log::ins()->try_append("[ERROR]", "[%u]%s:%d(%s): " fmt "\n", \
                gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
        } \
    } while (0)

#define FATAL(fmt, args...) \
    do \
    { \
        ring_log::ins()->try_append("[FATAL]", "[%u]%s:%d(%s): " fmt "\n", \
            gettid(), __FILE__, __LINE__, __FUNCTION__, ##args); \
    } while (0)

#endif
