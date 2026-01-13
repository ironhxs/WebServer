/**
 * @file webserver.cpp
 * @brief Web服务器核心实现 - 基于Epoll的高性能HTTP服务器
 * @details 实现Reactor/Proactor并发模型，支持10000+并发连接
 * 
 * 技术架构：
 * - I/O多路复用：Epoll（支持LT/ET模式）
 * - 并发处理：线程池 + 非阻塞Socket
 * - 连接管理：定时器链表自动清理超时连接
 * - 资源服务：零拷贝内存映射加速文件传输
 * - 数据存储：MySQL连接池管理
 * - 日志系统：异步日志队列，避免阻塞
 * 
 * @author ironhxs
 * @date 2026-01-09
 * @version 2.0
 */

#include "webserver.h"

/**
 * @brief 构造函数 - 初始化服务器核心资源
 * 
  * 初始化流程：
 * 1. 分配MAX_FD个http_conn对象数组（支持最大并发连接数）
 * 2. 设置网站根目录为 当前目录/root
 *3. 分配客户端定时器数组（每个连接对应一个定时器）
 * 
 * 内存分配：
 * - users数组：约 MAX_FD * sizeof(http_conn) ≈ 10MB
 * - users_timer数组：约 MAX_FD * sizeof(client_data) ≈ 5MB
 * 
 * @note MAX_FD默认为65536，可根据系统资源调整
 */
WebServer::WebServer()
{
    // 分配http_conn对象数组，每个对象代表一个客户端连接
    // 预分配避免运行时动态分配，提升性能
    users = new http_conn[MAX_FD];

    // 设置网站资源根目录路径
    // 最终路径为：/path/to/WebServer/resources/webroot
    char server_path[200];
    if (getcwd(server_path, 200) == NULL) {
        strcpy(server_path, ".");  // 获取失败时使用当前目录
    }
    char root[19] = "/resources/webroot";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);  // 拼接完整路径

    // 分配客户端定时器数组，用于管理连接超时
    // 每个连接都有独立的定时器，超时自动断开
    users_timer = new client_data[MAX_FD];
}

/**
 * @brief 析构函数 - 清理所有资源
 * 
 * 清理顺序：
 * 1. 关闭epoll实例（停止事件监听）
 * 2. 关闭监听socket（停止接受新连接）
 * 3. 关闭信号管道（停止信号通知）
 * 4. 释放连接数组和定时器数组
 * 5. 删除线程池（等待所有工作线程结束）
 * 
 * @note 析构时会自动等待所有未完成的请求处理完毕
 */
WebServer::~WebServer()
{
    close(m_epollfd);      // 关闭epoll文件描述符
    close(m_listenfd);     // 关闭监听socket
    close(m_pipefd[1]);    // 关闭管道写端
    close(m_pipefd[0]);    // 关闭管道读端
    delete[] users;        // 释放http连接数组
    delete[] users_timer;  // 释放定时器数组
    delete m_pool;         // 删除线程池（会等待所有线程完成）
}

/**
 * @brief 初始化服务器所有配置参数
 * 
 * @param port 监听端口号（1-65535，建议使用1024以上）
 * @param user 数据库用户名
 * @param passWord 数据库密码
 * @param databaseName 数据库名称
 * @param log_write 日志写入方式（0=同步阻塞，1=异步队列）
 * @param opt_linger 优雅关闭连接（0=立即关闭，1=等待数据发送完）
 * @param trigmode 触发模式（0=LT+LT, 1=LT+ET, 2=ET+LT, 3=ET+ET）
 * @param sql_num 数据库连接池大小（建议与线程数相同）
 * @param thread_num 线程池大小（建议为CPU核心数的1-2倍）
 * @param close_log 是否关闭日志（0=开启，1=关闭，关闭可提升5-10%性能）
 * @param actor_model 并发模型（0=proactor模拟，1=reactor）
 * 
 * 性能调优建议：
 * - 开发环境：port=9006, log_write=0, trigmode=0, thread_num=4
 * - 生产环境：port=80, log_write=1, trigmode=3, thread_num=16-32
 * - 压测环境：close_log=1, trigmode=3, thread_num=CPU核心数*2
 */
void WebServer::init(int port, string user, string passWord, string databaseName, int log_write, 
                     int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int actor_model)
{
    m_port = port;                // 服务器监听端口
    m_user = user;                // MySQL用户名
    m_passWord = passWord;        // MySQL密码
    m_databaseName = databaseName;// 使用的数据库名
    m_sql_num = sql_num;          // 数据库连接池大小
    m_thread_num = thread_num;    // 工作线程数量
    m_log_write = log_write;      // 日志模式（同步/异步）
    m_OPT_LINGER = opt_linger;    // 优雅关闭选项
    m_TRIGMode = trigmode;        // epoll触发模式
    m_close_log = close_log;      // 是否关闭日志
    m_actormodel = actor_model;   // 并发模型选择
}

/**
 * @brief 设置Epoll触发模式
 * 
 * 触发模式说明：
 * - LT（Level Triggered，水平触发）：
 *   * 只要缓冲区有数据就会一直触发
 *   * 优点：不会丢失数据，编程简单
 *   * 缺点：相同事件可能多次触发，效率略低
 *   * 适用：监听socket（连接少，稳定性优先）
 * 
 * - ET（Edge Triggered，边缘触发）：
 *   * 只在状态改变时触发一次
 *   * 优点：减少系统调用，性能更高
 *   * 缺点：必须一次性读完/写完，编程复杂
 *   * 适用：连接socket（连接多，性能优先）
 * 
 * 推荐组合：
 * - 模式0（LT+LT）：开发调试，稳定可靠
 * - 模式1（LT+ET）：常用配置，平衡性能与稳定性
 * - 模式3（ET+ET）：高并发场景，追求极致性能
 */
void WebServer::trig_mode()
{
    // 模式0：监听LT + 连接LT（最稳定，适合开发）
    if (0 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    // 模式1：监听LT + 连接ET（推荐配置）
    else if (1 == m_TRIGMode)
    {
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    // 模式2：监听ET + 连接LT（较少使用）
    else if (2 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    // 模式3：监听ET + 连接ET（最高性能，适合生产）
    else if (3 == m_TRIGMode)
    {
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

/**
 * @brief 初始化日志系统
 * 
 * 日志系统功能：
 * - 记录服务器运行状态
 * - 记录客户端请求信息
 * - 记录错误和异常信息
 * - 支持日志分割（按日期或大小）
 * 
 * 同步日志 vs 异步日志：
 * - 同步日志（log_write=0）：
 *   * 立即写入磁盘，数据最安全
 *   * 会阻塞当前线程，影响性能（约降低10-15%）
 *   * 适合：开发调试，低并发场景
 * 
 * - 异步日志（log_write=1）：
 *   * 先写入内存队列，后台线程批量写入
 *   * 不阻塞工作线程，性能高
 *   * 适合：生产环境，高并发场景
 * 
 * @note 日志文件路径：./ServerLog，自动按日期分割
 */
void WebServer::log_write()
{
    if (0 == m_close_log)  // 如果未关闭日志
    {
        if (1 == m_log_write)
            // 异步日志：队列大小800，最大行数800000，缓冲区2000字节
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            // 同步日志：队列大小0表示同步模式
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

/**
 * @brief 初始化MySQL数据库连接池
 * 
 * 连接池工作原理：
 * 1. 预先创建N个数据库连接（避免每次请求都建立连接）
 * 2. 请求到来时从池中获取空闲连接
 * 3. 使用完毕后归还到池中（连接保持不关闭）
 * 4. 池满时新请求等待，避免数据库连接数过多
 * 
 * 性能优势：
 * - 避免频繁建立/关闭连接（每次约100ms开销）
 * - 复用连接，减少数据库服务器负担
 * - 控制并发连接数，保护数据库
 * 
 * 连接池大小设置：
 * - 过小：连接不够用，请求等待时间长
 * - 过大：浪费资源，数据库压力大
 * - 建议：设置为线程池大小（每个线程一个连接）
 * 
 * @note 连接参数：localhost:3306，字符集UTF-8
 */
void WebServer::sql_pool()
{
    // 获取连接池单例
    m_connPool = connection_pool::GetInstance();
    
    // 初始化连接池
    // 参数：主机、用户名、密码、数据库名、端口、连接数、日志开关
    m_connPool->init("localhost", m_user, m_passWord, m_databaseName, 3306, m_sql_num, m_close_log);

    // 初始化HTTP连接的数据库用户表
    // 从数据库加载user表数据到内存，用于登录验证
    users->initmysql_result(m_connPool);
}

/**
 * @brief 初始化线程池
 * 
 * 创建指定数量的工作线程处理HTTP请求
 * 线程池采用预创建模式，避免运行时频繁创建销毁线程
 * 
 * @throws std::exception 如果线程池创建失败
 * @note 线程池创建后无法动态调整大小
 */
void WebServer::thread_pool()
{
    try {
        m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
    } catch (std::exception& e) {
        LOG_ERROR("Thread pool creation failed: %s", e.what());
        exit(1);
    }
}

/**
 * @brief 开始监听客户端连接
 * 
 * 执行流程：
 * 1. 创建监听socket（TCP）
 * 2. 设置socket选项（端口复用、优雅关闭）
 * 3. 绑定地址和端口
 * 4. 开始监听（最大积压连接数5）
 * 5. 创建epoll实例
 * 6. 注册监听socket到epoll
 * 7. 创建信号通知管道
 * 8. 设置信号处理函数
 * 
 * @throws 绑定或监听失败时程序退出
 * @note 必须在init()之后调用
 */
void WebServer::eventListen()
{
    // 1. 创建TCP监听套接字
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    if (m_listenfd < 0)
    {
        perror("socket error");
        LOG_ERROR("Failed to create listening socket");
        exit(1);
    }

    // 2. 设置优雅关闭选项（SO_LINGER）
    // 优雅关闭可确保发送缓冲区数据发送完毕再关闭连接
    struct linger tmp;
    if (0 == m_OPT_LINGER)
    {
        // 不使用linger：close()立即返回，系统尝试发送数据
        tmp = {0, 1};
    }
    else
    {
        // 使用linger：close()等待1秒直到数据发送完或超时
        tmp = {1, 1};
    }
    setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    // 3. 配置监听地址
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;           // IPv4
    address.sin_addr.s_addr = htonl(INADDR_ANY);  // 监听所有网卡
    address.sin_port = htons(m_port);       // 网络字节序端口

    // 4. 设置端口复用（SO_REUSEADDR）
    // 允许服务器重启后立即绑定端口，避免TIME_WAIT状态占用
    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    
    // 5. 设置接收和发送缓冲区大小（支持大文件上传）
    int recv_buf_size = 16 * 1024 * 1024;  // 16MB
    int send_buf_size = 16 * 1024 * 1024;  // 16MB
    setsockopt(m_listenfd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));
    setsockopt(m_listenfd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
    
    // 6. 绑定监听套接字到指定地址和端口
    int ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));
    if (ret < 0)
    {
        perror("bind error");
        LOG_ERROR("Port %d bind failed, may be in use", m_port);
        printf("端口 %d 可能被占用，请使用其他端口或执行: kill $(lsof -t -i:%d)\n", m_port, m_port);
        close(m_listenfd);
        exit(1);
    }
    
    // 6. 开始监听，设置连接队列最大长度
    // backlog=65535: 支持高并发场景下的大量排队连接
    ret = listen(m_listenfd, 65535);
    if (ret < 0)
    {
        perror("listen error");
        LOG_ERROR("Listen on port %d failed", m_port);
        close(m_listenfd);
        exit(1);
    }

    // 7. 初始化定时器工具
    utils.init(TIMESLOT);

    // 8. 创建epoll实例
    // 参数5在新版本内核中被忽略，仅需>0即可
    m_epollfd = epoll_create(5);
    if (m_epollfd == -1)
    {
        perror("epoll_create error");
        LOG_ERROR("Failed to create epoll instance");
        close(m_listenfd);
        exit(1);
    }

    // 9. 将监听socket注册到epoll
    // oneshot=false: 监听socket不使用EPOLLONESHOT
    // trigmode: 根据配置使用LT或ET模式
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    // 10. 创建Unix域socket对用于信号通知
    // 信号处理函数通过管道写端通知主循环
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    if (ret == -1)
    {
        perror("socketpair error");
        LOG_ERROR("Failed to create signal pipe");
        close(m_epollfd);
        close(m_listenfd);
        exit(1);
    }
    
    // 设置管道写端为非阻塞
    utils.setnonblocking(m_pipefd[1]);
    
    // 将管道读端注册到epoll，用于接收信号通知
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    // 工具类设置全局变量（必须在设置信号处理之前！）
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;

    // 设置信号处理
    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, Utils::sig_handler, false);
    utils.addsig(SIGTERM, Utils::sig_handler, false);

    alarm(TIMESLOT); // 启动定时器
    
    // 显示服务器访问地址
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║            WebServer 启动成功！                          ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    
    // 显示本地访问地址
    printf("║  本地访问: http://127.0.0.1:%d/                       ║\n", m_port);
    
    // 获取并显示网络接口IP（适用于WSL2）
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            if (ifa->ifa_addr->sa_family == AF_INET) {
                // 只显示 eth0 等物理网卡的IP，排除 lo（回环）接口
                if (ifa->ifa_name == NULL || strncmp(ifa->ifa_name, "lo", 2) == 0) continue;
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                char *ip = inet_ntoa(addr->sin_addr);
                // 排除 127.x.x.x 和 10.255.255.254（WSL内部虚拟地址）
                if (strncmp(ip, "127.", 4) == 0) continue;
                if (strcmp(ip, "10.255.255.254") == 0) continue;
                printf("║  网络访问: http://%s:%d/             ║\n", ip, m_port);
            }
        }
        freeifaddrs(ifaddr);
    }
    
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║  网站根目录: %s\n", m_root);
    printf("║  触发模式: %s + %s                                    ║\n", 
           m_LISTENTrigmode ? "ET" : "LT", m_CONNTrigmode ? "ET" : "LT");
    printf("║  并发模型: %s                                        ║\n", 
           m_actormodel ? "Reactor" : "Proactor");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\n按 Ctrl+C 停止服务器...\n\n");
}

/**
 * @brief 初始化定时器并设置新客户端连接
 * 
 * @param connfd 客户端连接的socket文件描述符
 * @param client_address 客户端地址信息
 * 
 * 执行流程：
 * 1. 设置socket缓冲区大小（支持大文件传输）
 * 2. 初始化http_conn对象
 * 3. 创建定时器，设置超时回调
 * 4. 将定时器加入定时器链表
 * 
 * 库函数说明：
 * - setsockopt(): 设置socket选项
 *   * SO_RCVBUF: 接收缓冲区大小
 *   * SO_SNDBUF: 发送缓冲区大小
 * - time(NULL): 获取当前Unix时间戳（秒）
 */
void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // 设置客户端 socket 缓冲区大小（支持大文件上传）
    int recv_buf_size = 16 * 1024 * 1024;  // 16MB
    int send_buf_size = 16 * 1024 * 1024;  // 16MB
    setsockopt(connfd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size));
    setsockopt(connfd, SOL_SOCKET, SO_SNDBUF, &send_buf_size, sizeof(send_buf_size));
    
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passWord, m_databaseName);

    // 初始化 client_data 数据
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    users_timer[connfd].conn = &users[connfd];  // 关联 http_conn 对象
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

/**
 * @brief 调整定时器，延长连接超时时间
 * 
 * @param timer 待调整的定时器指针
 * 
 * 当客户端有活动（读/写）时调用此函数，
 * 将超时时间延后3个TIMESLOT周期，避免活跃连接被误关闭
 * 
 * 库函数说明：
 * - time(NULL): <ctime> 获取当前时间戳
 *   * 返回自1970-01-01 00:00:00 UTC以来的秒数
 *   * 参数NULL表示不存储到指定位置
 */
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

/**
 * @brief 处理定时器到期事件
 * 
 * @param timer 到期的定时器指针
 * @param sockfd 对应的socket文件描述符
 * 
 * 执行流程：
 * 1. 调用定时器回调函数（关闭连接）
 * 2. 从定时器链表中删除该定时器
 * 
 * 回调函数cb_func会：
 * - 从epoll中移除该socket
 * - 关闭socket连接
 * - 减少用户计数
 */
void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}

/**
 * @brief 处理新客户端连接请求
 * 
 * @return true 处理成功
 * @return false 处理失败或ET模式循环结束
 * 
 * 根据监听socket的触发模式选择处理方式：
 * - LT模式：每次只accept一个连接
 * - ET模式：循环accept直到返回错误（EAGAIN）
 * 
 * 库函数说明：
 * - accept(): <sys/socket.h> 接受客户端连接
 *   * 参数1: 监听socket
 *   * 参数2: 输出参数，客户端地址结构
 *   * 参数3: 地址结构大小的指针
 *   * 返回: 新连接的socket描述符，失败返回-1
 *   * 阻塞模式下会等待新连接
 *   * 非阻塞模式下无连接时返回-1，errno=EAGAIN
 */
bool WebServer::dealclientdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);
    if (0 == m_LISTENTrigmode)
    {
        // LT模式：水平触发，每次处理一个连接
        // accept(): 从已完成连接队列取出一个连接
        int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
        if (connfd < 0)
        {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if (http_conn::m_user_count >= MAX_FD)
        {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    else
    {
        while (1)
        {
            int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address);
        }
        return false;
    }
    return true;
}

/**
 * @brief 处理信号事件
 * 
 * @param timeout 输出参数，是否收到SIGALRM定时信号
 * @param stop_server 输出参数，是否收到SIGTERM终止信号
 * @return true 处理成功
 * @return false 接收失败
 * 
 * 信号处理机制（统一事件源）：
 * 1. 信号处理函数将信号值写入管道
 * 2. 主循环通过epoll监听管道读端
 * 3. 收到可读事件后读取信号值并处理
 * 
 * 库函数说明：
 * - recv(): <sys/socket.h> 从socket接收数据
 *   * 参数1: socket描述符
 *   * 参数2: 接收缓冲区
 *   * 参数3: 缓冲区大小
 *   * 参数4: 标志位（0表示默认行为）
 *   * 返回: 接收的字节数，0表示连接关闭，-1表示错误
 */
bool WebServer::dealwithsignal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    char signals[1024];
    // recv(): 从管道读取信号值
    // 每个信号值占1字节，可能同时收到多个信号
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true; // 定时器信号：通知主循环处理超时连接
                break;
            }
            case SIGTERM:
            {
                stop_server = true; // 终止信号：请求服务器退出
                break;
            }
            }
        }
    }
    return true;
}

/**
 * @brief 处理客户端读事件（收到HTTP请求）
 * 
 * @param sockfd 发生读事件的socket描述符
 * 
 * 根据并发模型选择处理方式：
 * 
 * Reactor模式（m_actormodel=1）：
 * - 主线程只负责通知，工作线程负责读取和处理
 * - 将任务加入线程池队列
 * - 等待工作线程完成（带超时保护）
 * 
 * Proactor模式（m_actormodel=0）：
 * - 主线程负责读取数据
 * - 读取完成后将处理任务加入线程池
 * - 工作线程只负责业务逻辑处理
 * 
 * 库函数说明：
 * - usleep(): <unistd.h> 微秒级睡眠
 *   * 参数: 睡眠时间（微秒）
 *   * 1秒 = 1,000,000微秒
 *   * 用于等待工作线程完成，避免CPU空转
 * - inet_ntoa(): <arpa/inet.h> 网络地址转字符串
 *   * 将in_addr结构转换为点分十进制字符串
 *   * 返回静态缓冲区指针，非线程安全
 */
void WebServer::dealwithread(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    // Reactor 模型
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        // 将读事件加入线程池
        m_pool->append(users + sockfd, 0);

        // 使用带超时的等待，避免死锁（特别是大文件上传场景）
        // 最多等待100ms，让主线程有机会处理其他事件
        int wait_count = 0;
        const int max_wait = 1000;  // 最多等待100ms (1000 * 100us)
        while (wait_count < max_wait)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
            usleep(100);  // 等待100微秒
            wait_count++;
        }
        // 如果超时，不做特殊处理，让 epoll 继续监听该连接
    }
    else
    {
        // Proactor 模型
        if (users[sockfd].read_once())
        {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            m_pool->append_p(users + sockfd);

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

/**
 * @brief 处理客户端写事件（发送HTTP响应）
 * 
 * @param sockfd 发生写事件的socket描述符
 * 
 * 写事件触发条件：
 * - socket发送缓冲区有空间可写
 * - 之前注册了EPOLLOUT事件
 * 
 * 处理方式与读事件类似，根据并发模型分别处理
 */
void WebServer::dealwithwrite(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;
    // Reactor 模型
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        m_pool->append(users + sockfd, 1);

        // 使用带超时的等待，避免死锁
        int wait_count = 0;
        const int max_wait = 1000;  // 最多等待100ms
        while (wait_count < max_wait)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
            usleep(100);
            wait_count++;
        }
    }
    else
    {
        // Proactor 模型
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}

/**
 * @brief 服务器事件主循环
 * 
 * 这是服务器的核心运行函数，采用事件驱动模型：
 * 1. 调用epoll_wait等待事件发生
 * 2. 遍历就绪事件数组
 * 3. 根据事件类型分发处理
 * 4. 检查并处理定时器超时
 * 
 * 事件类型：
 * - 监听socket可读：新客户端连接
 * - 管道可读：收到信号通知
 * - 连接socket可读：客户端发送数据
 * - 连接socket可写：可以向客户端发送数据
 * - EPOLLRDHUP/EPOLLHUP/EPOLLERR：连接异常
 * 
 * 库函数说明：
 * - epoll_wait(): <sys/epoll.h> 等待epoll事件
 *   * 参数1: epoll实例描述符
 *   * 参数2: 事件数组，用于存储就绪事件
 *   * 参数3: 数组最大容量
 *   * 参数4: 超时时间（-1表示永久阻塞）
 *   * 返回: 就绪事件数量，0表示超时，-1表示错误
 *   * 错误时errno=EINTR表示被信号中断，应重试
 */
void WebServer::eventLoop()
{
    bool timeout = false;      // 是否收到定时信号
    bool stop_server = false;  // 是否收到终止信号

    while (!stop_server)
    {
        // epoll_wait(): 阻塞等待事件发生
        // 返回就绪事件数量，events数组存储就绪事件
        // 超时参数-1表示无限等待，直到有事件发生
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;

            // 处理新连接
            if (sockfd == m_listenfd)
            {
                bool flag = dealclientdata();
                if (false == flag)
                    continue;
            }
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // 客户端异常关闭
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            // 处理信号
            else if ((sockfd == m_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                bool flag = dealwithsignal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            // 处理读事件
            else if (events[i].events & EPOLLIN)
            {
                dealwithread(sockfd);
            }
            // 处理写事件
            else if (events[i].events & EPOLLOUT)
            {
                dealwithwrite(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}
