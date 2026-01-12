/**
 * @file log.h
 * @brief 日志系统接口定义
 * @details 支持同步/异步日志写入，提供统一的日志宏封装。
 */

#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "blocking_queue.h"

using namespace std;

/**
 * @class Log
 * @brief 日志类（单例）
 */
class Log
{
public:
    /**
     * @brief 获取日志单例实例
     * @return Log实例指针
     * @note C++11局部静态变量线程安全，无需加锁
     */
    static Log *get_instance()
    {
        static Log instance;
        return &instance;
    }

    /**
     * @brief 异步写日志线程入口
     * @param args 线程参数（未使用）
     * @return 线程返回值
     */
    static void *flush_log_thread(void *args)
    {
        (void)args;
        Log::get_instance()->async_write_log();
        return nullptr;
    }
    /**
     * @brief 初始化日志系统
     * @param file_name 日志文件名/路径
     * @param close_log 是否关闭日志（0=开启，1=关闭）
     * @param log_buf_size 日志缓冲区大小
     * @param split_lines 单文件最大行数
     * @param max_queue_size 异步队列长度（>0开启异步）
     * @return 初始化是否成功
     */
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    /**
     * @brief 写入一条日志
     * @param level 日志级别（0=debug,1=info,2=warn,3=error）
     * @param format 格式化字符串
     */
    void write_log(int level, const char *format, ...);

    /**
     * @brief 立即刷新日志到文件
     */
    void flush(void);

private:
    /**
     * @brief 构造函数（私有，单例）
     */
    Log();
    /**
     * @brief 析构函数
     */
    virtual ~Log();
    /**
     * @brief 异步写日志主循环
     * @return 线程返回值
     */
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
        return nullptr;
    }

private:
    char dir_name[128];              ///< 日志目录路径
    char log_name[128];              ///< 日志文件名
    int m_split_lines;               ///< 单文件最大行数
    int m_log_buf_size;              ///< 日志缓冲区大小
    long long m_count;               ///< 已写入日志行数
    int m_today;                     ///< 当前日期（按天切分）
    FILE *m_fp;                      ///< 日志文件指针
    char *m_buf;                     ///< 日志缓冲区
    block_queue<string> *m_log_queue;///< 异步日志队列
    bool m_is_async;                 ///< 是否异步写入
    locker m_mutex;                  ///< 写日志互斥锁
    int m_close_log;                 ///< 日志开关
};

/**
 * @brief Debug级别日志宏
 */
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
/**
 * @brief Info级别日志宏
 */
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
/**
 * @brief Warn级别日志宏
 */
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
/**
 * @brief Error级别日志宏
 */
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif
