#ifndef LST_TIMER
#define LST_TIMER

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>

#include <time.h>
#include "../Log/log.hpp"

class util_timer;


/*用户数据结构：客户端socket地址、socket文件描述符、读缓存和定时器*/
struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

/*定时器类*/
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire; /*任务的超时时间，这里使用绝对时间*/
    void (*cb_func)(client_data*); /*任务回调函数*/
    /*回调函数处理的客户数据，由定时器的执行者传递给回调函数*/
    client_data* user_data;
    util_timer* prev; /*指向前一个定时器*/
    util_timer* next; /*指向下一个定时器*/
};

/*定时器链表。它是一个升序、双向链表，且带有头结点和尾节点*/
class sort_timer_lst
{
public:
    /**
     * @brief 定时器链表的构造函数
    */
    sort_timer_lst();
    /*链表被销毁时，删除其中所有的定时器*/
    ~sort_timer_lst();

    /*将目标定时器timer添加到链表中*/
    void add_timer(util_timer *timer);

    /*当某个定时任务发生变化时，调整对应的定时器在链表中的位置。
    这个函数只考虑被调整的定时器的超时时间延长的情况，
    即该定时器需要往链表的尾部移动*/
    void adjust_timer(util_timer *timer);

    /*将目标定时器timer从链表中删除*/
    void del_timer(util_timer *timer);

    /* SIGALRM信号每次被触发就在其信号处理函数（如果使用统一事件源，则是主函数）
    中执行一次tick函数，以处理链表上到期的任务 */
    void tick();

private:
    /**
     * @brief 一个重载的辅助函数，它被公有的add_timer函数和adjust_timer函数调用。该
     *        函数表示将目标定时器timer添加到节点lst_head之后的部分链表中
    */
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head;
    util_timer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
