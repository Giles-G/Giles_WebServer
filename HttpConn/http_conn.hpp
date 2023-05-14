#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
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
#include <map>

#include "../Lock/locker.hpp"
#include "../ConnPool/sql_connection_pool.hpp"
#include "../Timer/timer.hpp"
#include "../Log/log.hpp"

class http_conn
{
public:
    /*文件名的最大长度*/
    static const int FILENAME_LEN = 200;
    /*读缓冲区的大小*/
    static const int READ_BUFFER_SIZE = 2048;
    /*写缓冲区的大小*/
    static const int WRITE_BUFFER_SIZE = 1024;
    /*HTTP请求方法*/
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
     /*解析客户请求时，主状态机所处的状态*/
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    /*服务器处理HTTP请求的可能结果*/
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    /*行的读取状态*/
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    /**
     * @brief 初始化连接,外部调用初始化套接字地址，并将sockfd加入epfd
     *        空位置分别是服务器根地址、触发模式、日志开启标志
     * @param sockfd 客户端的socket套接字
     * @param addr 客户端的地址
     */
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);

    /**
     * @brief 关闭连接，关闭一个连接，客户总量减一：操作就是将m_sockfd从epfd中移除
     * @param real_close 是否真的关闭
     */
    void close_conn(bool real_close = true);

    /**
     * @brief 总体处理http的请求事件，就是先读取客户端的http请求，然后回送响应报文；
     *        即先调用 process_read，再调用process_write
     */
    void process();

    /**
     * @brief 当epoll通知新事件到来的时候，读取客户数据；在非阻塞ET工作模式下，需要一次性将数据读完
     * @return 是否完整的成功读取
     */
    bool read_once();

    /**
     * @brief 将客户端请求的内容发送给客户端，即将数据写入到m_sockfd；
     *        使用writev函数同时发送应答报文的首部字段和请求内容
     */
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag; // 这是个什么b玩意
    int improv;     // 这是个什么b玩意


private:
    /**
     * @brief 参数初始化
      */
    void init();

    /**
     * @brief 读取客户端http报文的主状态机
     * @return 请求结果
     */
    HTTP_CODE process_read();

    /**
     * @brief 处理服务器将http应答报文发送给客户端的过程
     * @return 请求结果
     */
    bool process_write(HTTP_CODE ret);

    /*下面这一组函数被process_read调用以分析HTTP请求*/

    /**
     * @brief 解析http请求行，获得请求方法，目标url及http版本号；
     *        最后要更带状态机状态到分析头部字段
     * @param text 传入的请求行内容
     * @return 请求结果
     */
    HTTP_CODE parse_request_line(char *text);

    /**
     * @brief 解析http请求的一个头部字段
     * @param text 传入的请求行内容
     * @return 请求结果
     */
    HTTP_CODE parse_headers(char *text);

    /**
     * @brief 解析http请求的内容实体
     * @param text 传入的请求行内容
     * @return 请求结果
     */
    HTTP_CODE parse_content(char *text);

    /**
     * @brief 客户端的http请求读取完毕后，开始处理请求，主要是控制html页面的跳转工作。
     */
    HTTP_CODE do_request();

    /**
     * @brief 获得当前读缓冲区的起始位置，即找到从哪里开始读
     * @return 当前读缓冲区的起始位置
     */
    char *get_line() { return m_read_buf + m_start_line; };

    /**
     * @brief 从状态机，用于解析出一行内容，其实就是寻找/r/n，这一行有可能是请求行，也有可能是头部行;
     * @return 行的读取状态:成功、失败、行不完整
      */
    LINE_STATUS parse_line();

    /*下面这一组函数被process_write调用以填充HTTP应答*/

    /**
     * @brief munmap执行与mmap相反的操作，删除特定地址区域的对象映射。
     */
    void unmap();

    /* 下面的几组函数都是用于添加http响应报文 ：如添加请求行、添加首部行（添加首部字段）、添加内容实体、添加空行等*/
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    /*所有socket上的事件都被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为静态的*/
    static int m_epollfd;
    /*统计用户数量*/
    static int m_user_count;
    /* mysql句柄 */
    MYSQL *mysql;
    /* 读为0, 写为1 */
    int m_state; 

private:
    /*该HTTP连接的socket和对方的socket地址*/    
    int m_sockfd;
    sockaddr_in m_address;
    /*读缓冲区*/
    char m_read_buf[READ_BUFFER_SIZE];
    /*标识读缓冲中已经读入的客户数据的最后一个字节的下一个位置*/
    int m_read_idx;
    /*当前正在分析的字符在读缓冲区中的位置*/
    int m_checked_idx;
    /*当前正在解析的行的起始位置*/
    int m_start_line;
    /*写缓冲区*/
    char m_write_buf[WRITE_BUFFER_SIZE];
    /*写缓冲区中待发送的字节数*/
    int m_write_idx;
    /*主状态机当前所处的状态*/
    CHECK_STATE m_check_state;
    /*请求方法*/
    METHOD m_method;
    /*客户请求的目标文件的完整路径，其内容等于doc_root+m_url，doc_root是网站根目录*/
    char m_real_file[FILENAME_LEN];
    /*客户请求的目标文件的文件名*/
    char* m_url;
    /*HTTP协议版本号，我们仅支持HTTP/1.1*/
    char* m_version;
    /*主机名*/
    char* m_host;
    /*HTTP请求的消息体的长度*/
    int m_content_length;
    /*HTTP请求是否要求保持连接*/
    bool m_linger;
    /*客户请求的目标文件被mmap到内存中的起始位置*/
    char* m_file_address;
    /*目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息*/
    struct stat m_file_stat;
    /*我们将采用writev来执行写操作，将http头和客户端请求的文件一起写入*/
    struct iovec m_iv[2];
    /* 被写内存块的数量 */
    int m_iv_count;
    /* 是否开启POST */
    int cgi;
    /* 存储请求头数据 */
    char* m_string;
    /* 服务器 需要 向客户端发送的应答报文的字节数 */
    int bytes_to_send;
    /* 服务器 已经 向客户端发送的应答报文的字节数 */
    int bytes_have_send;
    /* 请求内容的根目录 */
    char *doc_root;

    /* 用于记录用户和密码的哈希表 */
    map<string, string> m_users;
    int m_TRIGMode;
    /* 标志位：是否关闭日志 */
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
