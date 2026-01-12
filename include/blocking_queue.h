/**
 * @file blocking_queue.h
 * @brief 循环数组实现的阻塞队列
 * @details 线程安全的有界队列，使用互斥锁与条件变量实现生产者/消费者模型。
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
     * @brief 入队一个元素
     * @param item 入队元素
     * @return 成功返回true，满队列返回false
     */
    bool push(const T &item)
    {

        m_mutex.lock();
        if (m_size >= m_max_size)
        {

            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    /**
     * @brief 出队一个元素（阻塞等待）
     * @param item 输出元素
     * @return 成功返回true
     */
    bool pop(T &item)
    {

        m_mutex.lock();
        while (m_size <= 0)
        {
            
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    /**
     * @brief 出队一个元素（带超时）
     * @param item 输出元素
     * @param ms_timeout 超时时间（毫秒）
     * @return 成功返回true，超时返回false
     */
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
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
