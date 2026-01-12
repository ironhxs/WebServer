/**
 * @file timer_list.cpp
 * @brief 定时器链表与工具类实现
 */

#include "timer_list.h"
#include "http_connection.h"

/**
 * @brief 定时器链表构造函数
 */
sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

/**
 * @brief 定时器链表析构函数
 */
sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

/**
 * @brief 将定时器插入链表
 * @param timer 定时器指针
 */
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // 如果链表为空，直接设置为头尾
    if (!head)
    {
        head = tail = timer;
        return;
    }
    // 如果定时器的过期时间比头节点还早，插入头部
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    // 插入到链表中的合适位置
    add_timer(timer, head);
}

/**
 * @brief 调整定时器位置
 * @param timer 定时器指针
 */
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    // 如果定时器已经是最后一个或调整后位置不变，直接返回
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    // 如果是头节点，调整为新的位置
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    else
    {
        // 从链表中移除该节点
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        // 插入到新的位置
        add_timer(timer, timer->next);
    }
}

/**
 * @brief 删除定时器
 * @param timer 定时器指针
 */
void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // 如果链表中只有一个定时器
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    // 如果是头节点
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    // 如果是尾节点
    if (timer == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }
    // 在链表中间删除
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

/**
 * @brief 处理到期定时器
 */
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    
    time_t cur = time(NULL); // 获取当前时间
    util_timer *tmp = head;
    while (tmp)
    {
        // 如果当前时间小于定时器的过期时间，停止处理
        if (cur < tmp->expire)
        {
            break;
        }
        // 调用定时器的回调函数
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}

/**
 * @brief 辅助插入函数（从指定节点开始）
 * @param timer 定时器指针
 * @param lst_head 起始节点
 */
void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next;
    while (tmp)
    {
        if (timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    // 如果到达链表尾部
    if (!tmp)
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

/**
 * @brief 工具类初始化
 * @param timeslot 定时器槽间隔
 */
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

/**
 * @brief 设置fd为非阻塞
 * @param fd 文件描述符
 * @return 原有flag
 */
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
 * @brief 注册fd到epoll
 * @param epollfd epoll实例fd
 * @param fd 目标fd
 * @param one_shot 是否使用EPOLLONESHOT
 * @param TRIGMode 触发模式
 */
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/**
 * @brief 信号处理函数（写入管道通知）
 * @param sig 信号编号
 */
void Utils::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    if (u_pipefd != nullptr) {
        send(u_pipefd[1], (char *)&msg, 1, 0);
    }
    errno = save_errno;
}

/**
 * @brief 设置信号处理函数
 * @param sig 信号编号
 * @param handler 处理函数
 * @param restart 是否自动重启系统调用
 */
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(1);
    }
}

/**
 * @brief 定时任务处理并重置alarm
 */
void Utils::timer_handler()
{
    m_timer_lst.tick(); // 处理链表中到期的定时器
    alarm(m_TIMESLOT); // 重新设置定时信号
}

/**
 * @brief 发送错误信息并关闭连接
 * @param connfd 连接fd
 * @param info 错误提示
 */
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

/**
 * @brief 静态成员初始化
 */
int *Utils::u_pipefd = nullptr;
int Utils::u_epollfd = 0;

/**
 * @brief 定时器回调函数 - 超时关闭连接
 * @param user_data 连接关联数据
 * @details 定时器超时时调用，负责清理连接并更新统计信息
 */
void cb_func(client_data *user_data)
{
    assert(user_data);
    
    // 如果有关联的 http_conn 对象，调用其 close_conn() 来正确更新IP统计
    if (user_data->conn)
    {
        user_data->conn->close_conn(true);
    }
    else
    {
        // 降级处理：直接关闭（旧代码逻辑）
        epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
        close(user_data->sockfd);
        http_conn::m_user_count--;
    }
}
