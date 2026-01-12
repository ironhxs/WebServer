/**
 * @file thread_sync.h
 * @brief 线程同步工具封装（信号量、互斥锁、条件变量）
 * @details 对pthread与POSIX信号量进行轻量封装，提供更易用的接口。
 */

#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

/**
 * @class sem
 * @brief 信号量封装类
 */
class sem
{
public:
    /**
     * @brief 构造函数 - 初始化信号量（初值为0）
     */
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    /**
     * @brief 构造函数 - 初始化信号量
     * @param num 初始计数
     */
    sem(int num)
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    /**
     * @brief 析构函数 - 释放信号量
     */
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    /**
     * @brief P操作（等待）
     * @return 成功返回true
     */
    bool wait()
    {
        return sem_wait(&m_sem) == 0;
    }
    /**
     * @brief V操作（通知）
     * @return 成功返回true
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
 */
class locker
{
public:
    /**
     * @brief 构造函数 - 初始化互斥锁
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
     */
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    /**
     * @brief 加锁
     * @return 成功返回true
     */
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    /**
     * @brief 解锁
     * @return 成功返回true
     */
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    /**
     * @brief 获取原生互斥锁指针
     * @return pthread_mutex_t指针
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
 */
class cond
{
public:
    /**
     * @brief 构造函数 - 初始化条件变量
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
     */
    ~cond()
    {
        pthread_cond_destroy(&m_cond);
    }
    /**
     * @brief 等待条件变量
     * @param m_mutex 关联的互斥锁
     * @return 成功返回true
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
     * @param t 超时时间
     * @return 成功返回true
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
     */
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    /**
     * @brief 唤醒所有等待线程
     * @return 成功返回true
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
