/**
 * @file webserver.h
 * @brief Web服务器核心类定义
 * @details 实现基于Epoll的高性能Web服务器，支持HTTP协议、数据库连接池、
 *          线程池、定时器等功能
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <ifaddrs.h>
#include <netdb.h>

#include "threadpool.h"
#include "http_connection.h"

// ========== 全局常量定义 ==========
const int MAX_FD = 10000;           ///< 系统最大文件描述符数量
const int MAX_EVENT_NUMBER = 10000; ///< epoll可监听的最大事件数
const int TIMESLOT = 5;             ///< 定时器最小超时单位（秒），每5秒检查一次超时

/**
 * @class WebServer
 * @brief Web服务器核心类
 * 
 * 功能特性：
 * - 基于Epoll的I/O多路复用
 * - 线程池并发处理请求
 * - MySQL数据库连接池
 * - 异步/同步日志系统
 * - 定时器管理非活动连接
 * - 支持LT/ET触发模式
 * - HTTP/1.1协议支持
 * 
 * 工作流程：
 * 1. init() - 初始化服务器参数
 * 2. log_write() - 初始化日志系统
 * 3. sql_pool() - 创建数据库连接池
 * 4. thread_pool() - 创建工作线程池
 * 5. trig_mode() - 设置触发模式
 * 6. eventListen() - 开始监听端口
 * 7. eventLoop() - 事件循环处理请求
 */
class WebServer
{
public:
    /**
     * @brief 构造函数 - 初始化服务器对象
     */
    WebServer();
    
    /**
     * @brief 析构函数 - 清理资源
     */
    ~WebServer();

    /**
     * @brief 初始化服务器参数
     * @param port 监听端口号
     * @param user 数据库用户名
     * @param passWord 数据库密码
     * @param databaseName 数据库名
     * @param log_write 日志写入方式(0=同步,1=异步)
     * @param opt_linger 优雅关闭连接(0=不使用,1=使用)
     * @param trigmode 触发模式(0=LT+LT,1=LT+ET,2=ET+LT,3=ET+ET)
     * @param sql_num 数据库连接池大小
     * @param thread_num 线程池大小
     * @param close_log 是否关闭日志(0=开启,1=关闭)
     * @param actor_model 并发模型(0=proactor,1=reactor)
     */
    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    /**
     * @brief 创建线程池
     */
    void thread_pool();
    /**
     * @brief 创建数据库连接池
     */
    void sql_pool();
    /**
     * @brief 初始化日志系统
     */
    void log_write();
    /**
     * @brief 设置触发模式
     */
    void trig_mode();
    /**
     * @brief 开始监听端口
     */
    void eventListen();
    /**
     * @brief 事件循环主函数
     */
    void eventLoop();
    
    /**
     * @brief 为新连接创建定时器
     * @param connfd 连接的文件描述符
     * @param client_address 客户端地址信息
     */
    void timer(int connfd, struct sockaddr_in client_address);
    
    /**
     * @brief 调整定时器（延长超时时间）
     * @param timer 定时器指针
     */
    void adjust_timer(util_timer *timer);
    
    /**
     * @brief 处理超时连接，关闭连接并删除定时器
     * @param timer 定时器指针
     * @param sockfd 连接的文件描述符
     */
    void deal_timer(util_timer *timer, int sockfd);
    
    /**
     * @brief 处理新客户端连接
     * @return 是否处理成功
     */
    bool dealclientdata();
    /**
     * @brief 处理信号事件
     * @param timeout 是否超时
     * @param stop_server 是否停止服务
     * @return 是否处理成功
     */
    bool dealwithsignal(bool& timeout, bool& stop_server);
    /**
     * @brief 处理读事件
     * @param sockfd 连接fd
     */
    void dealwithread(int sockfd);
    /**
     * @brief 处理写事件
     * @param sockfd 连接fd
     */
    void dealwithwrite(int sockfd);

public:
    // ========== 基础配置 ==========
    int m_port;                  ///< 监听端口号
    char *m_root;                ///< 网站根目录路径
    int m_log_write;             ///< 日志写入方式
    int m_close_log;             ///< 是否关闭日志
    int m_actormodel;            ///< 并发模型选择

    int m_pipefd[2];             ///< 管道文件描述符，用于信号通知
    int m_epollfd;               ///< epoll实例文件描述符
    http_conn *users;            ///< HTTP连接数组

    // ========== 数据库相关 ==========
    connection_pool *m_connPool; ///< 数据库连接池指针
    string m_user;               ///< 数据库登录用户名
    string m_passWord;           ///< 数据库登录密码
    string m_databaseName;       ///< 使用的数据库名
    int m_sql_num;               ///< 数据库连接池大小

    // ========== 线程池相关 ==========
    threadpool<http_conn> *m_pool; ///< 线程池指针
    int m_thread_num;              ///< 线程池中线程数量

    // ========== Epoll事件相关 ==========
    epoll_event events[MAX_EVENT_NUMBER]; ///< epoll事件数组

    int m_listenfd;              ///< 监听socket文件描述符
    int m_OPT_LINGER;            ///< 是否使用优雅关闭
    int m_TRIGMode;              ///< 触发模式组合
    int m_LISTENTrigmode;        ///< 监听socket触发模式
    int m_CONNTrigmode;          ///< 连接socket触发模式

    // ========== 定时器相关 ==========
    /**
     * @brief 客户端定时器数组（索引与fd一一对应）
     * @details 用于保存每个连接的地址、sockfd及定时器指针，
     *          便于超时回调快速定位并关闭连接。
     */
    client_data *users_timer;    ///< （client_data）：负责超时管理（保存地址、fd、定时器指针）
    Utils utils;                 ///< 工具类对象
};
#endif
