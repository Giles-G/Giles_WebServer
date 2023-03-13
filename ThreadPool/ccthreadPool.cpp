#include "./ccthreadPool.hpp"

void TaskQueue::addTask(Task& task) {
    std::lock_guard<std::mutex> locker(
        m_mutex);  // 访问任务队列必须加锁（无论读写）
    m_queue.push(task);
}

void TaskQueue::addTask(callback func, void* arg) {
    std::lock_guard<std::mutex> locker(m_mutex);
    Task task(func, arg);
    m_queue.push(task);
}

Task TaskQueue::takeTask() {
    Task task;
    std::lock_guard<std::mutex> locker(m_mutex);
    if (m_queue.size() > 0) {
        task = m_queue.front();
        m_queue.pop();
    }
    return task;
}

/************* 下面是ThreadPool的类定义 ***********/

ThreadPool::ThreadPool(int minNum, int maxNum) {
    do {
        m_minNum = minNum;
        m_maxNum = maxNum;
        m_busyNum = 0;
        m_aliveNum = minNum;
        m_exitNum = 0;

        // 根据线程池的最大上限给工作线程数组分配内存
        m_threadIDs.resize(m_maxNum);
        if (m_threadIDs.empty()) {
            std::cout << "向工作线程组分配内存时出现错误..." << std::endl;
            break;
        }

        // 初始化, 互斥锁和条件变量在cpp中都不用显式初始化

        /************* 创建线程 ************/
        // 根据最小数量创建线程
        for (int i = 0; i < m_minNum; i++) {
            // 一定上加上this，要不然线程找不到函数入口，报的错还看不懂
            m_threadIDs[i] = std::thread(worker, this);
            std::cout << "创建子线程, ID: " << m_threadIDs[i].get_id()
                      << std::endl;
        }

        m_managerID = std::thread(manager, this);
    } while (0);
}

ThreadPool::~ThreadPool() {
    m_shutdown = 1;
    // 销毁管理者线程
    m_managerID.join();
    // 唤醒所有的消费者线程，让他们把活干完
    for (int i = 0; i < m_aliveNum; i++) {
        m_notEmpty.notify_all();
    }

    if (!m_threadIDs.empty())
        for (int i = 0; i < m_maxNum; i++)
            if (m_threadIDs[i].joinable())
                m_threadIDs[i].join();
    // cpp中的mutex和信号量好像不用销毁
}

void ThreadPool::addTask(Task task) {
    std::lock_guard<std::mutex> locker(m_lock);
    if (m_shutdown)
        return;

    m_taskQ.push(task);
    // 唤醒工作线程
    m_notEmpty.notify_all();
}

int ThreadPool::getAliveNumber() {
    int threadNum = 0;
    std::lock_guard<std::mutex> locker(m_lock);
    threadNum = m_aliveNum;
    return threadNum;
}

int ThreadPool::getBusyNumber() {
    int threadNum = 0;
    std::lock_guard<std::mutex> locker(m_lock);
    threadNum = m_busyNum;
    return threadNum;
}

void* ThreadPool::worker(void* arg) {
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    while (true) {
        // 访问任务队列加锁
        std::unique_lock<std::mutex> locker(pool->m_lock);
        // 这里的wait的第二个参数是返回bool的函数，返回true的时候结束阻塞，返回false继续阻塞
        while (pool->m_taskQ.size() == 0 && !pool->m_shutdown) {
            pool->m_notEmpty.wait(locker);

            // 判断是否需要销毁线程
            if (pool->m_exitNum > 0) {
                pool->m_exitNum--;
                if (pool->m_aliveNum > pool->m_minNum) {
                    pool->m_aliveNum--;
                    // 在范围内有重复上锁，所以要提前解锁
                    locker.unlock();
                    // pool->threadExit();
                    cout << "thread " << std::this_thread::get_id()
                         << " exit working..." << endl;
                    return nullptr;
                }
            }
        }
        // pool->m_notEmpty.wait(locker, [pool]() {
        //     return pool->m_taskQ.size() != 0 || pool->m_shutdown;});
        // 解除阻塞之后, 判断是否要销毁线程

        // 判断线程池是否被关闭
        if (pool->m_shutdown) {
            cout << "thread " << std::this_thread::get_id()
                 << " exit working..." << endl;
            // pool->threadExit();
            return nullptr;
        }

        locker.unlock();

        pool->m_lock.lock();
        // 从任务队列中取出一个任务
        Task task = pool->m_taskQ.front();
        pool->m_taskQ.pop();
        // 线程的数量+1
        pool->m_busyNum++;
        pool->m_lock.unlock();

        // 执行任务
        cout << "thread " << std::this_thread::get_id() << " start working..."
             << endl;
        task.function(task.arg);
        free(task.arg);
        task.arg = nullptr;

        // 任务处理结束
        cout << "thread " << std::this_thread::get_id() << " end working..."
             << endl;

        pool->m_lock.lock();
        pool->m_busyNum--;
        pool->m_lock.unlock();
    }

    return nullptr;
}

// 管理者线程函数
void* ThreadPool::manager(void* arg) {
    ThreadPool* pool = static_cast<ThreadPool*>(arg);
    /* 工作线程设置为true是因为需要将剩余任务处理完，然后在工作线程内部关闭 */
    /* 但是管理者线程要及时关闭 */
    while (!pool->m_shutdown) {
        // 每间隔5秒检测一次
        sleep(5);
        // 取出工作线程的相关数量
        pool->m_lock.lock();
        int queueSize = pool->m_taskQ.size();
        int liveNum = pool->m_aliveNum;
        int busyNum = pool->m_busyNum;
        pool->m_lock.unlock();

        // 创建线程, 一次最多两个
        const int NUMBER = 2;
        // 当前任务个数>存活的线程数 && 存活的线程数<最大线程个数
        if (queueSize > liveNum && liveNum < pool->m_maxNum) {
            // 线程池加锁
            std::unique_lock<std::mutex> locker(pool->m_lock);
            int num = 0;
            for (int i = 0; i < pool->m_maxNum && num < NUMBER &&
                            pool->m_aliveNum < pool->m_maxNum;
                 i++) {
                if (pool->m_threadIDs[i].joinable() == false) {
                    pool->m_threadIDs[i] = std::thread(worker, pool);
                    num++;
                    pool->m_aliveNum++;
                }
            }
        }

        // 销毁多余的线程
        // 忙线程*2 < 存活的线程数目 && 存活的线程数 > 最小线程数量
        if (busyNum * 2 < liveNum && liveNum > pool->m_minNum) {
            pool->m_lock.lock();
            pool->m_exitNum = NUMBER;  // 一次性销毁两个线程
            pool->m_lock.unlock();
            for (int i = 0; i < NUMBER; i++) {
                pool->m_notEmpty.notify_all();
            }
        }
    }
    return nullptr;
}

// 线程退出
/* void ThreadPool::threadExit() {
    auto tid = std::this_thread::get_id();
    for (int i = 0; i < m_maxNum; ++i) {
        if (m_threadIDs[i].get_id() == tid) {
            cout << "threadExit() function: thread " << m_threadIDs[i].get_id()
                 << " exiting..." << endl;
            m_threadIDs[i].detach();
            break;
        }
    }
    // pthread_exit(NULL);
} */
