#include "timer_list.h"
#include "http_connection.h"

// 定时器链表的构造函数，初始化头尾指针
sort_timer_lst::sort_timer_lst()
{
    head = NULL;
    tail = NULL;
}

// 定时器链表的析构函数，释放所有定时器
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

// 添加定时器到链表中
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

// 调整定时器的位置
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

// 删除定时器
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

// 定时器到期处理函数
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

// 添加定时器到链表中的辅助函数
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

// 工具类初始化函数
void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

// 设置文件描述符为非阻塞模式
int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 注册读事件到内核事件表，支持 ET 模式和 EPOLLONESHOT
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

// 信号处理函数，将信号发送到管道
void Utils::sig_handler(int sig)
{
    int save_errno = errno; // 保存原来的 errno
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0); // 将信号写入管道
    errno = save_errno; // 恢复 errno
}

// 设置信号处理函数
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，并重新设置定时器以持续触发 SIGALRM 信号
void Utils::timer_handler()
{
    m_timer_lst.tick(); // 处理链表中到期的定时器
    alarm(m_TIMESLOT); // 重新设置定时信号
}

// 显示错误信息并关闭连接
void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

// 静态成员变量初始化
int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

// 定时器回调函数，关闭客户端连接并减少用户计数
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
}
