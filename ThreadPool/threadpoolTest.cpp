// #include <pthread.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <string>
// #include "./threadPool.h"
#include "./ccthreadPool.hpp"

void taskFunc(void* arg) {
    int num = *(int*)arg;
    std::cout << "thread " << std::this_thread::get_id()
              << " is working, number =  " << num << std::endl;
    std::cout << std::endl;
    sleep(1);
}

int main() {
    // 创建线程池
    ThreadPool pool(3, 10);
    for (int i = 0; i < 100; ++i) {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        Task t(taskFunc, num);
        pool.addTask(t);
    }

    sleep(20);
    return 0;
}

/* void taskFunc(void* arg) {
    int num = *(int*)arg;
    printf("thread %ld is working, number = %d\n", pthread_self(), num);
    sleep(1);
}

int main() {
    // 创建线程池
    ThreadPool* pool = threadPoolCreate(3, 10, 100);
    for (int i = 0; i < 100; ++i) {
        int* num = (int*)malloc(sizeof(int));
        *num = i + 100;
        threadPoolAdd(pool, taskFunc, num);
    }

    sleep(20);

    threadPoolDestroy(pool);
    return 0;
} */