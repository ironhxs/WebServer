/**
 * @file thread_sync.h
 * @brief 线程同步工具封装（信号量、互斥锁、条件变量）
 * @details 对pthread与POSIX信号量进行轻量封装，提供更易用的接口
 * 
 * 三种同步原语对比:
 * 
 * 1. 信号量 (Semaphore)
 *    - 用途: 资源计数，控制并发访问数量
 *    - 例如: 连接池中的可用连接数量
 *    - 操作: wait(P操作减1) / post(V操作加1)
 *    - 可跨进程使用
 * 
 * 2. 互斥锁 (Mutex)
 *    - 用途: 保护临界区，保证同一时刻只有一个线程访问
 *    - 例如: 保护共享数据结构的读写
 *    - 操作: lock() / unlock()
 *    - 必须由同一线程加锁和解锁
 * 
 * 3. 条件变量 (Condition Variable)
 *    - 用途: 线程间的事件通知，等待某个条件成立
 *    - 例如: 生产者-消费者模型中的队列非空通知
 *    - 操作: wait()等待 / signal()唤醒一个 / broadcast()唤醒所有
 *    - 必须与互斥锁配合使用
 * 
 * POSIX线程库头文件:
 * - <pthread.h>: 互斥锁和条件变量
 * - <semaphore.h>: POSIX信号量
 */

#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/**
 * @class sem
 * @brief 信号量封装类
 * @details POSIX信号量是一个整数计数器，用于控制并发访问
 * 
 * 应用场景:
 * - 资源池管理（如数据库连接池）
 * - 线程池任务队列计数
 * - 生产者-消费者同步
 * 
 * 与互斥锁的区别:
 * - 信号量可以大于1（允许多个线程同时访问）
 * - 不需要由同一线程wait和post
 * - 可用于进程间同步（pshared=1）
 */
class sem
{
public:
    /**
     * @brief 构造函数 - 初始化信号量（初值为0）
     * @throws std::exception 初始化失败时抛出异常
     * 
     * 初值为0的常见用途:
     * - 等待某个事件发生（wait阻塞直到post）
     * - 实现线程同步点
     */
    sem()
    {
        /**
         * sem_init() - 初始化未命名信号量
         * @param sem: 信号量对象指针
         * @param pshared: 0=线程间共享, 非0=进程间共享
         * @param value: 初始计数值
         * @return: 成功返回0，失败返回-1
         * 
         * 与命名信号量的区别:
         * - sem_init(): 未命名，通常用于线程同步
         * - sem_open(): 命名，用于进程间同步，有文件系统持久化
         */
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    /**
     * @brief 构造函数 - 初始化信号量为指定初值
     * @param num 初始计数值
     * @throws std::exception 初始化失败时抛出异常
     * 
     * 常用初值:
     * - num=N: 允许N个线程同时访问资源
     * - 如数据库连接池初始化为连接数量
     */
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    /**
     * @brief 析构函数 - 释放信号量资源
     * 
     * sem_destroy() - 销毁未命名信号量
     * @param sem: 信号量对象指针
     * @return: 成功返回0
     * 
     * 注意: 销毁正在被wait的信号量会导致未定义行为
     */
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    /**
     * @brief P操作（等待/获取）
     * @return 成功返回true
     * 
     * sem_wait() - 信号量减1
     * - 如果计数>0，立即减1并返回
     * - 如果计数=0，阻塞等待直到>0
     * - 原子操作，线程安全
     * 
     * 相关函数:
     * - sem_trywait(): 非阻塞版本，失败返回EAGAIN
     * - sem_timedwait(): 超时版本
     */
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    /**
     * @brief V操作（释放/通知）
     * @return 成功返回true
     * 
     * sem_post() - 信号量加1
     * - 立即将计数加1
     * - 如果有线程在wait，唤醒其中一个
     * - 原子操作，线程安全
     * - 可在信号处理函数中安全调用（async-signal-safe）
     */
    bool post()
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem; ///< POSIX信号量对象
};

/**
 * @class locker
 * @brief 互斥锁封装类
 * @details 保护临界区，保证同一时刻只有一个线程执行
 * 
 * 应用场景:
 * - 保护共享变量的读写
 * - 保护共享数据结构（链表、队列等）
 * - 配合条件变量使用
 * 
 * 与C++11 std::mutex的区别:
 * - pthread_mutex是POSIX标准，跨平台性稍差
 * - std::mutex是C++标准，更现代且跨平台
 * - pthread_mutex支持更多类型（递归锁、读写锁等）
 */
class locker
{
public:
    /**
     * @brief 构造函数 - 初始化互斥锁
     * @throws std::exception 初始化失败时抛出异常
     * 
     * pthread_mutex_init() - 初始化互斥锁
     * @param mutex: 互斥锁对象指针
     * @param attr: 属性（NULL使用默认属性）
     * @return: 成功返回0
     * 
     * 常用互斥锁类型（通过attr设置）:
     * - PTHREAD_MUTEX_NORMAL: 普通锁（默认）
     * - PTHREAD_MUTEX_RECURSIVE: 递归锁（同一线程可重复加锁）
     * - PTHREAD_MUTEX_ERRORCHECK: 错误检查锁
     */
    locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    /**
     * @brief 析构函数 - 销毁互斥锁
     * 
     * pthread_mutex_destroy() - 销毁互斥锁
     * @param mutex: 互斥锁对象指针
     * @return: 成功返回0
     * 
     * 注意: 销毁已锁定的互斥锁会导致未定义行为
     */
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    /**
     * @brief 加锁
     * @return 成功返回true
     * 
     * pthread_mutex_lock() - 加锁（阻塞）
     * - 如果锁空闲，立即获取锁并返回
     * - 如果锁被占用，阻塞等待直到获取
     * - 死锁防范: 同一线程重复加锁会死锁（除非用递归锁）
     * 
     * 相关函数:
     * - pthread_mutex_trylock(): 非阻塞，失败返回EBUSY
     * - pthread_mutex_timedlock(): 超时版本
     */
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    /**
     * @brief 解锁
     * @return 成功返回true
     * 
     * pthread_mutex_unlock() - 解锁
     * - 释放锁，允许其他等待线程获取
     * - 必须由加锁的线程来解锁
     * - 解锁未持有的锁会导致未定义行为
     * 
     * 最佳实践: 使用RAII封装确保必定解锁
     */
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    /**
     * @brief 获取原生互斥锁指针
     * @return pthread_mutex_t指针
     * 
     * 用途: 传递给条件变量的wait函数
     */
    pthread_mutex_t *get()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex; ///< POSIX互斥锁
};

/**
 * @class cond
 * @brief 条件变量封装类
 * @details 线程间的事件通知机制，必须与互斥锁配合使用
 * 
 * 使用模式 (wait端):
 * @code
 * mutex.lock();
 * while (!condition) {       // 必须用while，防止spurious wakeup
 *     cond.wait(mutex.get());  // 原子地释放锁并等待
 * }
 * // 条件满足，处理数据...
 * mutex.unlock();
 * @endcode
 * 
 * 使用模式 (signal端):
 * @code
 * mutex.lock();
 * // 修改共享数据，使条件成立
 * cond.signal();  // 或 broadcast()
 * mutex.unlock();
 * @endcode
 * 
 * 为什么要配合互斥锁:
 * - 保护条件判断和等待的原子性
 * - 避免信号丢失（lost wakeup）
 * - 保护共享数据的一致性
 */
class cond
{
public:
    /**
     * @brief 构造函数 - 初始化条件变量
     * @throws std::exception 初始化失败时抛出异常
     * 
     * pthread_cond_init() - 初始化条件变量
     * @param cond: 条件变量对象指针
     * @param attr: 属性（NULL使用默认）
     * @return: 成功返回0
     */
    cond()
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    /**
     * @brief 析构函数 - 销毁条件变量
     * 
     * pthread_cond_destroy() - 销毁条件变量
     * 注意: 销毁正在被wait的条件变量会导致未定义行为
     */
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    /**
     * @brief 等待条件变量（阻塞）
     * @param m_mutex 关联的互斥锁（必须已锁定）
     * @return 成功返回true
     * 
     * pthread_cond_wait() - 等待条件变量
     * @param cond: 条件变量
     * @param mutex: 关联的互斥锁
     * @return: 成功返回0
     * 
     * 关键操作流程:
     * 1. 原子地释放互斥锁
     * 2. 将线程加入条件变量等待队列
     * 3. 阻塞等待被唤醒
     * 4. 被唤醒后重新获取互斥锁
     * 5. 返回（此时持有锁）
     * 
     * 虚假唤醒(spurious wakeup):
     * - 线程可能在未收到signal时被唤醒
     * - 因此必须用while循环检查条件
     */
    bool wait(pthread_mutex_t *m_mutex)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    /**
     * @brief 超时等待条件变量
     * @param m_mutex 关联的互斥锁
     * @param t 绝对超时时间
     * @return 成功返回true，超时返回false
     * 
     * pthread_cond_timedwait() - 超时等待
     * @param cond: 条件变量
     * @param mutex: 关联的互斥锁
     * @param abstime: 绝对超时时间(struct timespec)
     * @return: 成功返回0，超时返回ETIMEDOUT
     * 
     * struct timespec {
     *     time_t tv_sec;   // 秒
     *     long   tv_nsec;  // 纳秒
     * };
     * 
     * 注意: abstime是绝对时间，不是相对时间
     */
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    /**
     * @brief 唤醒一个等待线程
     * @return 成功返回true
     * 
     * pthread_cond_signal() - 唤醒一个等待线程
     * - 如果有多个线程等待，只唤醒其中一个
     * - 调度策略由系统决定（通常FIFO）
     * - 如果没有等待线程，信号会丢失
     */
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    /**
     * @brief 唤醒所有等待线程
     * @return 成功返回true
     * 
     * pthread_cond_broadcast() - 唤醒所有等待线程
     * - 所有等待的线程都会被唤醒
     * - 线程会依次获取互斥锁（不是同时）
     * - 适用于多个线程都可能处理的情况
     * 
     * signal() vs broadcast():
     * - signal: 在确定只有一个等待者时使用
     * - broadcast: 在多个等待者且条件都可能满足时使用
     */
    bool broadcast()
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond; ///< POSIX条件变量
};
#endif
