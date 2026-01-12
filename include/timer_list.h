/**
 * @file timer_list.h
 * @brief 定时器链表与辅助工具类
 * @details 提供按超时排序的双向链表及与epoll/信号相关的辅助函数，
 *          用于管理连接超时并触发回调。
 */

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
#include "log.h"

class util_timer;

/**
 * @struct client_data
 * @brief 连接对应的数据结构（与定时器关联）
 */
struct client_data
{
    sockaddr_in address; ///< 客户端地址信息
    int sockfd;          ///< 连接的文件描述符
    util_timer *timer;   ///< 关联的定时器指针
};

/**
 * @class util_timer
 * @brief 定时器节点（链表元素）
 */
class util_timer
{
public:
    /**
     * @brief 构造函数 - 初始化前后指针
     */
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;                 ///< 超时时刻（绝对时间）
    void (*cb_func)(client_data *);///< 超时回调函数
    client_data *user_data;        ///< 回调关联的连接数据
    util_timer *prev;              ///< 前驱节点
    util_timer *next;              ///< 后继节点
};

/**
 * @class sort_timer_lst
 * @brief 按超时排序的双向定时器链表
 */
class sort_timer_lst
{
public:
    /**
     * @brief 构造函数 - 初始化空链表
     */
    sort_timer_lst();
    /**
     * @brief 析构函数 - 释放所有定时器节点
     */
    ~sort_timer_lst();

    /**
     * @brief 插入定时器节点
     * @param timer 待插入的定时器
     */
    void add_timer(util_timer *timer);

    /**
     * @brief 调整定时器位置（超时时间变更后）
     * @param timer 需要调整的定时器
     */
    void adjust_timer(util_timer *timer);

    /**
     * @brief 从链表中删除定时器
     * @param timer 需要删除的定时器
     */
    void del_timer(util_timer *timer);

    /**
     * @brief 执行到期定时器并触发回调
     */
    void tick();

private:
    /**
     * @brief 从指定节点开始插入定时器
     * @param timer 待插入的定时器
     * @param lst_head 起始节点
     */
    void add_timer(util_timer *timer, util_timer *lst_head);

    util_timer *head; ///< 链表头节点
    util_timer *tail; ///< 链表尾节点
};

/**
 * @class Utils
 * @brief epoll与信号相关的辅助工具
 */
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    /**
     * @brief 初始化工具类（设置定时器间隔）
     * @param timeslot 定时器槽间隔（秒）
     */
    void init(int timeslot);

    /**
     * @brief 设置文件描述符为非阻塞
     * @param fd 文件描述符
     * @return 原有的文件描述符标志位
     */
    int setnonblocking(int fd);

    /**
     * @brief 注册文件描述符到epoll
     * @param epollfd epoll实例fd
     * @param fd 目标fd
     * @param one_shot 是否使用EPOLLONESHOT
     * @param TRIGMode 触发模式（0=LT，1=ET）
     */
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    /**
     * @brief 信号处理函数（写入管道通知主循环）
     * @param sig 信号编号
     */
    static void sig_handler(int sig);

    /**
     * @brief 设置信号处理函数
     * @param sig 信号编号
     * @param handler 处理函数
     * @param restart 是否自动重启被中断的系统调用
     */
    void addsig(int sig, void(handler)(int), bool restart = true);

    /**
     * @brief 定时器处理入口（触发链表tick并重置alarm）
     */
    void timer_handler();

    /**
     * @brief 发送错误信息并关闭连接
     * @param connfd 连接fd
     * @param info 错误提示信息
     */
    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;    ///< 信号通知管道fd
    sort_timer_lst m_timer_lst; ///< 定时器链表
    static int u_epollfd;    ///< epoll实例fd
    int m_TIMESLOT;          ///< 定时器槽间隔（秒）
};

/**
 * @brief 定时器回调函数（关闭连接并更新计数）
 * @param user_data 连接关联数据
 */
void cb_func(client_data *user_data);

#endif
