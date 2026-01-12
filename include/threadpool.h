/**
 * @file threadpool.h
 * @brief 线程池模板类 - 实现高并发任务调度
 * @details 基于半同步/半反应堆模式，支持Reactor和Proactor两种并发模型
 * 
 * 线程池设计思想：
 * 1. 预先创建固定数量的工作线程（避免频繁创建销毁开销）
 * 2. 主线程将任务添加到请求队列
 * 3. 工作线程从队列取任务并处理
 * 4. 使用信号量同步，互斥锁保护队列
 * 
 * 并发模型：
 * - Proactor模式（actor_model=0）：
 *   * 主线程完成I/O读写操作
 *   * 工作线程只负责业务逻辑处理
 *   * 优点：工作线程专注计算，逻辑清晰
 *   * 适用：I/O密集型应用
 * 
 * - Reactor模式（actor_model=1）：
 *   * 工作线程自己完成I/O读写和业务处理
 *   * 主线程只负责监听事件通知
 *   * 优点：充分利用多核CPU，吞吐量高
 *   * 适用：计算密集型应用
 * 
 * @tparam T 任务类型（通常为http_conn）
 * @author Your Name
 * @date 2026-01-09
 * @version 2.0
 */

#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "thread_sync.h"
#include "database_pool.h"

/**
 * @class threadpool
 * @brief 线程池类 - 管理工作线程和任务队列
 * 
 * 核心组件：
 * - 工作线程数组：预先创建的线程
 * - 任务队列：待处理的请求
 * - 互斥锁：保护队列的线程安全
 * - 信号量：通知线程有新任务
 * 
 * 工作流程：
 * 1. 构造时创建N个工作线程，进入等待状态
 * 2. append()添加任务到队列，post()信号量唤醒一个线程
 * 3. 工作线程wait()等待信号，获取任务，执行处理
 * 4. 处理完成后继续wait()等待下一个任务
 * 
 * 性能特点：
 * - 避免线程创建销毁开销（每次约1-2ms）
 * - 限制并发任务数，防止资源耗尽
 * - 负载均衡：任务自动分配给空闲线程
 */
template <typename T>
class threadpool
{
public:
    /**
     * @brief 构造函数 - 创建线程池
     * 
     * @param actor_model 并发模型（0=Proactor，1=Reactor）
     * @param connPool 数据库连接池指针
     * @param thread_number 线程池中的线程数量
     *        建议值：CPU核心数 * 2
     *        过小：无法充分利用CPU
     *        过大：上下文切换开销增大
     * @param max_request 请求队列最大长度
     *        建议值：10000
     *        过小：高并发时任务被拒绝
     *        过大：占用内存过多
     * 
     * @throws std::exception 参数非法或资源分配失败
     * 
     * @note 线程创建后立即分离（detach），无需手动join
     */
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    
    /**
     * @brief 析构函数 - 清理线程池资源
     * @note 析构时线程仍在运行，需确保主程序不会立即退出
     */
    ~threadpool();
    
    /**
     * @brief 向任务队列添加任务（Reactor模式）
     * 
     * @param request 任务对象指针（http_conn*）
     * @param state 任务状态（0=读任务，1=写任务）
     * @return true 添加成功
     * @return false 队列已满，任务被拒绝
     * 
     * 使用场景：Reactor模式下，工作线程需要知道是读还是写
     * 
     * 线程安全：使用互斥锁保护队列操作
     */
    bool append(T *request, int state);
    
    /**
     * @brief 向任务队列添加任务（Proactor模式）
     * 
     * @param request 任务对象指针（http_conn*）
     * @return true 添加成功
     * @return false 队列已满，任务被拒绝
     * 
     * 使用场景：Proactor模式下，I/O已完成，直接处理业务逻辑
     */
    bool append_p(T *request);

private:
    /**
     * @brief 工作线程入口函数（静态成员）
     * 
     * @param arg 线程池对象指针（this）
     * @return void* 返回线程池指针
     * 
     * @note 必须是静态函数才能传给pthread_create
     */
    static void *worker(void *arg);
    
    /**
     * @brief 工作线程主循环 - 不断从队列取任务执行
     * 
     * 执行流程：
     * 1. wait()信号量，阻塞等待任务
     * 2. 加锁，从队列头取出任务
     * 3. 解锁，执行任务处理
     * 4. 根据并发模型选择处理方式
     * 5. 继续循环等待下一个任务
     * 
     * @note 死循环，线程永远不会退出
     */
    void run();

private:
    int m_thread_number;           ///< 线程池中的线程数量
    int m_max_requests;            ///< 请求队列允许的最大请求数
    pthread_t *m_threads;          ///< 线程ID数组，大小为m_thread_number
    std::list<T *> m_workqueue;    ///< 请求队列（FIFO），存储任务指针
    locker m_queuelocker;          ///< 保护请求队列的互斥锁
    sem m_queuestat;               ///< 信号量，表示是否有任务需要处理
    connection_pool *m_connPool;   ///< 数据库连接池指针
    int m_actor_model;             ///< 并发模型切换（0=Proactor，1=Reactor）
};

/**
 * @brief 构造函数实现 - 创建并启动所有工作线程
 * 
 * 初始化步骤：
 * 1. 参数校验（线程数和队列大小必须>0）
 * 2. 分配线程ID数组内存
 * 3. 循环创建N个工作线程
 * 4. 将线程设置为分离状态（detach）
 * 
 * detach的作用：
 * - 线程结束后自动释放资源，无需join
 * - 主线程无需等待工作线程结束
 * - 适合长期运行的守护线程
 * 
 * @note 如果任何一步失败，会清理已分配资源并抛出异常
 */
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) 
    : m_thread_number(thread_number), 
      m_max_requests(max_requests), 
      m_threads(NULL),
      m_connPool(connPool),
      m_actor_model(actor_model)
{
    // 参数有效性检查
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
        
    // 分配线程ID数组
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
        
    // 创建thread_number个工作线程
    for (int i = 0; i < thread_number; ++i)
    {
        // 创建线程，入口函数为worker，参数为this
        // worker是静态函数，通过this指针调用成员函数run()
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;  // 失败则清理已分配内存
            throw std::exception();
        }
        
        // 将线程设置为分离状态
        // 分离后线程结束时会自动释放资源
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

/**
 * @brief 析构函数实现 - 释放线程ID数组
 * @note 线程已分离，析构时自动清理，无需手动join
 */
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

/**
 * @brief 添加任务到队列（Reactor模式）
 * 
 * 流程：
 * 1. 加锁保护队列
 * 2. 检查队列是否已满
 * 3. 设置任务状态（读/写）
 * 4. 将任务添加到队列尾部
 * 5. 解锁
 * 6. post信号量通知工作线程
 * 
 * @note 使用RAII锁更安全，但这里手动lock/unlock性能更高
 */
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();  // 获取互斥锁
    
    // 队列已满，拒绝任务
    if (m_workqueue.size() >= static_cast<size_t>(m_max_requests))
    {
        m_queuelocker.unlock();
        return false;
    }
    
    request->m_state = state;  // 设置读写状态
    m_workqueue.push_back(request);  // 添加到队列尾部
    m_queuelocker.unlock();  // 释放锁
    m_queuestat.post();  // 信号量+1，唤醒一个等待线程
    return true;
}

/**
 * @brief 添加任务到队列（Proactor模式）
 * 
 * @note 与append()的区别：
 * - Proactor模式下，I/O已由主线程完成
 * - 工作线程只需处理业务逻辑
 * - 因此不需要设置m_state
 */
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    
    if (m_workqueue.size() >= static_cast<size_t>(m_max_requests))
    {
        m_queuelocker.unlock();
        return false;
    }
    
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

/**
 * @brief 工作线程入口函数（静态）
 * 
 * @param arg 线程池对象指针（this）
 * @return void* 返回线程池指针（无实际用途）
 * 
 * 为什么需要静态函数：
 * - pthread_create要求入口函数为全局函数或静态成员函数
 * - 静态函数没有this指针，通过参数传递
 * - 获取this后调用成员函数run()执行实际逻辑
 */
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;  // 类型转换获取线程池对象
    pool->run();  // 调用成员函数，进入工作循环
    return pool;
}

/**
 * @brief 工作线程主循环 - 核心处理逻辑
 * 
 * 流程：
 * 1. wait()阻塞等待信号量（有任务才被唤醒）
 * 2. 加锁，从队列头取出任务
 * 3. 解锁（尽快释放锁，提高并发）
 * 4. 根据并发模型执行不同处理：
 *    - Reactor：工作线程自己读/写+处理
 *    - Proactor：直接处理（I/O已完成）
 * 5. 回到步骤1，继续等待
 * 
 * 为什么是死循环：
 * - 工作线程需要持续服务，不能退出
 * - 进程结束时线程自动终止
 */
template <typename T>
void threadpool<T>::run()
{
    while (true)  // 无限循环，线程永不退出
    {
        // 等待信号量，阻塞直到有任务
        // 信号量>0时wait()返回，并将信号量-1
        m_queuestat.wait();
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;
        if (1 == m_actor_model)
        {
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    bool completed = request->process();
                    if (completed)
                    {
                        request->improv = 1;  // 请求完成，通知主线程
                    }
                    // 如果 !completed，不设置 improv，等待下一次 EPOLLIN
                    // 但主线程会超时退出，然后 epoll 会再次触发
                    else
                    {
                        request->improv = 1;  // 仍然设为1让主线程继续，等待下一次事件
                    }
                }
                else
                {
                    request->timer_flag = 1;
                    request->improv = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->timer_flag = 1;
                    request->improv = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
