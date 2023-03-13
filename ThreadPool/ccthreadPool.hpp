#ifndef _THREADPOOL_HPP_
#define _THREADPOOL_HPP_

#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <list>
#include <mutex>
#include <queue>
#include <thread>
#include <string>
#include <unistd.h>

using std::cout;
using std::endl;

// 回调函数类型——函数指针
using callback = void (*)(void*);

// 任务队列中的任务类型
struct Task {
    Task() {
        function = nullptr;
        arg = nullptr;
    }
    Task(callback f, void* arg) {
        function = f;
        this->arg = arg;
    }
    callback function;
    void* arg;
};

// 任务队列的声明
class TaskQueue {
   public:
    TaskQueue() = default;
    ~TaskQueue();

    // add task
    void addTask(Task& task);
    void addTask(callback func, void* arg);

    // 取出一个任务
    Task takeTask();

    // 获取当前队列中的任务个数
    inline int taskNumber() { return m_queue.size(); }

   private:
    std::mutex m_mutex;
    std::queue<Task> m_queue;
};

class ThreadPool {
   public:
    ThreadPool(int min, int max);
    ~ThreadPool();

    // 添加任务
    void addTask(Task task);
    // 获取忙线程的个数
    int getBusyNumber();
    // 获取活着的线程的数量
    int getAliveNumber();

   private:
    // 工作线程的任务函数
    static void* worker(void* arg);
    // 管理者线程的任务函数
    static void* manager(void* arg);
    // void threadExit();

   private:
    std::mutex m_lock;
    std::condition_variable m_notEmpty; // 注意，条件变量是“如果不满足当前条件，就阻塞当前线程，反之则继续运行”
    std::vector<std::thread> m_threadIDs; // 工作线程
    std::thread m_managerID;  // 管理者线程
    std::queue<Task> m_taskQ;       // 任务队列
    int m_minNum;
    int m_maxNum;
    int m_busyNum;
    int m_aliveNum;
    int m_exitNum;
    bool m_shutdown = false; // 标识当前线程池是否被关闭
};

#endif  // _THREADPOOL_H_