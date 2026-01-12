/**
 * @file blocking_queue.h
 * @brief 循环数组实现的阻塞队列
 * @details 线程安全的有界队列，使用互斥锁与条件变量实现生产者/消费者模型。
 * 
 * 核心同步原语说明：
 * 
 * 1. 互斥锁 (Mutex)
 *    - pthread_mutex_lock(): 获取锁，阻塞直到成功
 *    - pthread_mutex_unlock(): 释放锁
 *    - 用途：保护共享数据（队列）的互斥访问
 * 
 * 2. 条件变量 (Condition Variable)
 *    - pthread_cond_wait(): 释放锁并等待信号，被唤醒后重新获取锁
 *    - pthread_cond_signal(): 唤醒一个等待线程
 *    - pthread_cond_broadcast(): 唤醒所有等待线程
 *    - pthread_cond_timedwait(): 带超时的等待
 *    - 用途：线程间通知（生产者通知消费者有新数据）
 * 
 * 生产者-消费者模式：
 * - 生产者：push()添加元素，broadcast()通知消费者
 * - 消费者：pop()取出元素，队列空时wait()等待
 * 
 * 为什么用while而不是if检查条件：
 * - 虚假唤醒：cond_wait可能在没有信号时返回
 * - 惊群效应：broadcast唤醒所有线程，但只有一个能获取数据
 */

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "thread_sync.h"
using namespace std;

/**
 * @class block_queue
 * @brief 阻塞队列模板类（循环数组实现）
 * @tparam T 队列元素类型
 */
template <class T>
class block_queue
{
public:
    /**
     * @brief 构造函数
     * @param max_size 队列最大容量
     */
    block_queue(int max_size = 1000)
    {
        if (max_size <= 0)
        {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    /**
     * @brief 清空队列
     */
    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    /**
     * @brief 析构函数 - 释放队列内存
     */
    ~block_queue()
    {
        m_mutex.lock();
        if (m_array != NULL)
            delete [] m_array;

        m_mutex.unlock();
    }
    /**
     * @brief 判断队列是否已满
     * @return 已满返回true
     */
    bool full() 
    {
        m_mutex.lock();
        if (m_size >= m_max_size)
        {

            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    /**
     * @brief 判断队列是否为空
     * @return 为空返回true
     */
    bool empty() 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    /**
     * @brief 获取队首元素（不出队）
     * @param value 输出队首元素
     * @return 获取成功返回true
     */
    bool front(T &value) 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    /**
     * @brief 获取队尾元素（不出队）
     * @param value 输出队尾元素
     * @return 获取成功返回true
     */
    bool back(T &value) 
    {
        m_mutex.lock();
        if (0 == m_size)
        {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    /**
     * @brief 获取当前元素数量
     * @return 队列长度
     */
    int size() 
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_size;

        m_mutex.unlock();
        return tmp;
    }

    /**
     * @brief 获取队列最大容量
     * @return 最大容量
     */
    int max_size()
    {
        int tmp = 0;

        m_mutex.lock();
        tmp = m_max_size;

        m_mutex.unlock();
        return tmp;
    }
    /**
     * @brief 入队一个元素（生产者操作）
     * 
     * @param item 入队元素
     * @return true 成功入队
     * @return false 队列已满，入队失败
     * 
     * 执行流程：
     * 1. 加锁保护共享数据
     * 2. 检查队列是否已满
     * 3. 计算新的队尾位置（循环数组）
     * 4. 放入元素，更新计数
     * 5. broadcast()通知所有等待的消费者
     * 6. 解锁
     * 
     * 同步说明：
     * - m_mutex.lock(): 获取互斥锁
     * - m_cond.broadcast(): 唤醒所有在pop()中等待的消费者线程
     *   * 使用broadcast而非signal，确保不会遗漏消费者
     */
    bool push(const T &item)
    {

        m_mutex.lock();
        if (m_size >= m_max_size)
        {
            // 队列满，通知可能等待的消费者（虽然没用，但保持一致性）
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }
        // 循环数组：队尾指针回绕
        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size++;
        // 通知消费者有新数据可取
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    /**
     * @brief 出队一个元素（消费者操作，阻塞等待）
     * 
     * @param item 输出参数，存储出队的元素
     * @return true 成功出队
     * @return false 失败（被中断）
     * 
     * 执行流程：
     * 1. 加锁
     * 2. while循环检查队列是否为空
     * 3. 如果空，调用cond.wait()释放锁并阻塞
     * 4. 被唤醒后重新获取锁，再次检查（防止虚假唤醒）
     * 5. 取出队首元素，更新队首指针和计数
     * 6. 解锁
     * 
     * 同步说明：
     * - m_cond.wait(m_mutex.get()): 
     *   * 原子地释放锁并进入等待状态
     *   * 被signal/broadcast唤醒后重新获取锁
     *   * 必须在while循环中调用，处理虚假唤醒
     * 
     * 为什么用while不用if：
     * - 多个消费者被broadcast唤醒，但只有一个能取到数据
     * - 其他消费者需要重新wait
     */
    bool pop(T &item)
    {

        m_mutex.lock();
        // while循环：处理虚假唤醒和惊群效应
        while (m_size <= 0)
        {
            // wait()：释放锁，阻塞等待生产者的broadcast()
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }
        // 循环数组：队首指针回绕
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    /**
     * @brief 出队一个元素（带超时的阻塞等待）
     * 
     * @param item 输出参数，存储出队的元素
     * @param ms_timeout 超时时间（毫秒）
     * @return true 成功出队
     * @return false 超时或失败
     * 
     * 与普通pop()的区别：
     * - 使用timewait()代替wait()
     * - 超时后返回false，不会无限等待
     * - 适用于需要响应性的场景
     * 
     * 库函数说明：
     * - gettimeofday(): 获取当前时间（微秒精度）
     * - struct timespec: 纳秒级时间结构 {tv_sec, tv_nsec}
     * - pthread_cond_timedwait(): 带超时的条件等待
     *   * 参数为绝对时间，不是相对时间
     *   * 需要计算：当前时间 + 超时时长
     */
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        // 获取当前时间
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0)
        {
            // 计算超时的绝对时间
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            // timewait(): 带超时的条件等待
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;  // 超时返回
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    locker m_mutex; ///< 互斥锁
    cond m_cond;    ///< 条件变量

    T *m_array;     ///< 循环数组缓冲区
    int m_size;     ///< 当前元素数量
    int m_max_size; ///< 最大容量
    int m_front;    ///< 队首位置
    int m_back;     ///< 队尾位置
};

#endif
