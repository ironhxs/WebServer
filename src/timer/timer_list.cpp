/**
 * @file timer_list.cpp
 * @brief 定时器链表与工具类实现
 * @details 基于升序双向链表管理连接超时，结合信号机制实现定时检测
 * 
 * 主要使用的系统调用:
 * - time(): 获取当前时间戳（秒级）
 * - alarm(): 设置定时器，到期发送SIGALRM信号
 * - sigaction(): 设置信号处理函数
 * - sigfillset(): 初始化信号集为全集
 * - fcntl(): 文件描述符控制（设置非阻塞）
 * - epoll_ctl(): 管理epoll事件
 * - send(): 管道通信通知信号
 * - close(): 关闭文件描述符
 * 
 * 定时器设计:
 * - 升序链表: 按超时时间排序，tick()从头部开始检查
 * - 回调函数: 超时时自动关闭连接，清理资源
 * - 信号通知: 通过管道将信号通知主循环的epoll
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
 * @brief 处理到期定时器 - 链表的核心调度函数
 * @details 遍历链表，执行所有已超时定时器的回调函数
 * 
 * 算法流程:
 * 1. 获取当前时间戳
 * 2. 从头部开始遍历（头部是最早超时的）
 * 3. 比较当前时间与定时器超时时间
 * 4. 超时则执行回调并删除节点
 * 5. 未超时则停止遍历（后面都未超时）
 */
void sort_timer_lst::tick()
{
    if (!head)
    {
        return;
    }
    
    /**
     * time() - 获取当前时间
     * @param tloc: 如果不为NULL，也会将时间存储到此地址
     * @return: 返回自1970年1月1日以来的秒数（Unix时间戳）
     * 
     * 特点:
     * - 返回 time_t 类型（通常是64位整数）
     * - 精度为秒级，适合超时检测
     * - 失败返回(time_t)-1（极少失败）
     * - 传NULL可忽略参数直接使用返回值
     * 
     * 对比其他时间函数:
     * - gettimeofday(): 微秒级精度，但不单调
     * - clock_gettime(): 纳秒级，支持多种时钟源
     */
    time_t cur = time(NULL);
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
 * @brief 设置文件描述符为非阻塞模式
 * @param fd 文件描述符
 * @return 原有的文件状态标志
 * 
 * 非阻塞模式的作用:
 * - 读操作: 无数据时立即返回-1（EAGAIN）而不等待
 * - 写操作: 缓冲区满时立即返回-1而不等待
 * - 配合epoll: 实现高效的I/O多路复用
 */
int Utils::setnonblocking(int fd)
{
    /**
     * fcntl() - 文件描述符控制
     * @param fd: 文件描述符
     * @param cmd: 控制命令
     * @param arg: 命令参数（可选）
     * 
     * 常用命令:
     * - F_GETFL: 获取文件状态标志
     * - F_SETFL: 设置文件状态标志
     * - F_GETFD: 获取文件描述符标志
     * - F_SETFD: 设置文件描述符标志
     * - F_DUPFD: 复制文件描述符
     * 
     * 常用标志位:
     * - O_NONBLOCK: 非阻塞模式
     * - O_APPEND: 追加写入
     * - O_ASYNC: 异步I/O
     * 
     * 返回值: 成功返回标志值，失败返回-1
     */
    int old_option = fcntl(fd, F_GETFL);  // 获取当前标志
    int new_option = old_option | O_NONBLOCK;  // 添加非阻塞标志
    fcntl(fd, F_SETFL, new_option);  // 设置新标志
    return old_option;  // 返回原标志，便于恢复
}

/**
 * @brief 注册fd到epoll实例
 * @param epollfd epoll实例fd
 * @param fd 要注册的文件描述符
 * @param one_shot 是否启用EPOLLONESHOT
 * @param TRIGMode 触发模式 (0=LT, 1=ET)
 */
void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    /**
     * epoll事件标志说明:
     * - EPOLLIN: 可读事件（有数据可读）
     * - EPOLLOUT: 可写事件（可以写数据）
     * - EPOLLET: 边缘触发模式（状态变化时触发，需一次性读完）
     * - EPOLLRDHUP: 对端关闭连接或半关闭
     * - EPOLLONESHOT: 只触发一次，防止多线程同时处理同一fd
     * 
     * LT模式 vs ET模式:
     * - LT(水平触发): 只要有数据就一直触发，处理简单
     * - ET(边缘触发): 只在状态变化时触发一次，需循环读完
     */
    if (1 == TRIGMode)
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;  // ET模式
    else
        event.events = EPOLLIN | EPOLLRDHUP;  // LT模式

    // EPOLLONESHOT: 保证同一时刻只有一个线程处理该fd
    if (one_shot)
        event.events |= EPOLLONESHOT;
    
    /**
     * epoll_ctl() - 管理epoll实例中的文件描述符
     * @param epfd: epoll实例fd（epoll_create返回）
     * @param op: 操作类型
     *   - EPOLL_CTL_ADD: 添加新fd到epoll
     *   - EPOLL_CTL_MOD: 修改已注册fd的事件
     *   - EPOLL_CTL_DEL: 从 epoll删除fd
     * @param fd: 目标文件描述符
     * @param event: 事件结构体指针
     * @return: 成功返回0，失败返回-1
     */
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);  // 设置为非阻塞模式
}

/**
 * @brief 信号处理函数 - 将信号转换为管道消息
 * @param sig 收到的信号编号
 * 
 * 设计思想:
 * - 信号处理函数应尽可能简短（可重入性限制）
 * - 通过管道将信号通知到主循环的epoll
 * - 主循环统一处理信号，避免异步问题
 * 
 * 为什么保存和恢复errno:
 * - 信号可能在任意时刻中断代码
 * - send()可能修改errno
 * - 恢复errno保证被中断代码能正确检查错误
 */
void Utils::sig_handler(int sig)
{
    int save_errno = errno;  // 保存原始errno
    int msg = sig;
    if (u_pipefd != nullptr) {
        /**
         * send() - 通过管道发送信号值
         * 此处将信号编号作为1字节消息写入管道
         * 主循环epoll监听管道读端，收到消息后处理
         */
        send(u_pipefd[1], (char *)&msg, 1, 0);
    }
    errno = save_errno;  // 恢复errno
}

/**
 * @brief 设置信号处理函数
 * @param sig 信号编号 (SIGALRM, SIGTERM等)
 * @param handler 信号处理函数指针
 * @param restart 是否自动重启被中断的系统调用
 * 
 * 常用信号:
 * - SIGALRM: alarm()定时器到期信号
 * - SIGTERM: 终止信号（可被捕获）
 * - SIGINT: 中断信号 (Ctrl+C)
 * - SIGPIPE: 向已关闭的管道/socket写入
 */
void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    /**
     * struct sigaction 结构体:
     * - sa_handler: 信号处理函数指针（或SIG_IGN/SIG_DFL）
     * - sa_sigaction: 扩展处理函数（带额外信息）
     * - sa_mask: 处理时要阻塞的信号集
     * - sa_flags: 标志位
     *   - SA_RESTART: 自动重启被中断的系统调用
     *   - SA_SIGINFO: 使用sa_sigaction而非sa_handler
     */
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;  // 设置处理函数
    
    // SA_RESTART: 阻止信号中断导致系统调用返回EINTR
    if (restart)
        sa.sa_flags |= SA_RESTART;
    
    /**
     * sigfillset() - 将信号集初始化为包含所有信号
     * @param set: 信号集指针
     * 
     * 作用: 处理当前信号时阻塞其他所有信号，
     *       防止信号处理函数被中断
     * 
     * 其他信号集函数:
     * - sigemptyset(): 初始化为空集
     * - sigaddset(): 添加信号到集合
     * - sigdelset(): 从集合删除信号
     * - sigismember(): 检查信号是否在集合中
     */
    sigfillset(&sa.sa_mask);
    
    /**
     * sigaction() - 设置信号处理方式
     * @param signum: 信号编号
     * @param act: 新的处理方式
     * @param oldact: 输出旧的处理方式（可为NULL）
     * @return: 成功返回0，失败返回-1
     * 
     * vs signal(): sigaction()提供更精细的控制，
     *             且行为在不同系统上更一致
     */
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(1);
    }
}

/**
 * @brief 定时任务处理入口
 * @details 处理到期定时器并重新设置定时器
 * 
 * 工作流程:
 * 1. tick()检查并处理所有超时连接
 * 2. alarm()重新设置定时器，等待下次检查
 */
void Utils::timer_handler()
{
    m_timer_lst.tick();  // 处理链表中到期的定时器
    
    /**
     * alarm() - 设置定时器
     * @param seconds: 定时时间（秒）
     * @return: 返回上次alarm剩余秒数，或无上次则返回0
     * 
     * 工作机制:
     * 1. 当前进程在seconds秒后收到SIGALRM信号
     * 2. SIGALRM默认处理是终止进程
     * 3. 我们设置了sig_handler来处理
     * 4. 每次只能有一个alarm生效（新设置会取消旧的）
     * 
     * 与 setitimer() 对比:
     * - alarm(): 简单，秒级精度
     * - setitimer(): 微秒精度，支持周期性定时
     */
    alarm(m_TIMESLOT);  // 重新设置定时信号
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
 * 
 * 回调函数设计:
 * - 通过函数指针储存在定时器节点中
 * - tick()检测到超时时自动调用
 * - 传入user_data包含关闭连接所需的所有信息
 * 
 * 清理操作:
 * 1. 优先调用http_conn::close_conn()正确清理
 * 2. 降级处理: 直接epoll_ctl删除 + close()关闭
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
        /**
         * epoll_ctl(EPOLL_CTL_DEL) - 从 epoll 删除 fd
         * 停止监听该文件描述符的事件
         */
        epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
        
        /**
         * close() - 关闭文件描述符
         * @param fd: 要关闭的文件描述符
         * @return: 成功返回0，失败返回-1
         * 
         * 对于socket:
         * - 关闭连接，发送FIN包（正常关闭）
         * - 释放系统资源（端口、缓冲区等）
         * - fd可被重新分配给新连接
         */
        close(user_data->sockfd);
        http_conn::m_user_count--;  // 更新连接计数
    }
}
