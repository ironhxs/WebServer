/**
 * @file log.cpp
 * @brief 日志系统实现
 */

#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>
using namespace std;

/**
 * @brief 日志类构造函数
 */
Log::Log()
{
    m_count = 0;       // 日志条数计数
    m_is_async = false; // 默认同步模式
}

/**
 * @brief 日志类析构函数
 */
Log::~Log()
{
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}

/**
 * @brief 初始化日志系统
 * @param file_name 日志文件名/路径
 * @param close_log 是否关闭日志
 * @param log_buf_size 缓冲区大小
 * @param split_lines 单文件最大行数
 * @param max_queue_size 异步队列大小
 * @return 初始化是否成功
 */
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了 max_queue_size，则启用异步日志模式
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // 创建异步写日志的线程
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;         // 是否关闭日志
    m_log_buf_size = log_buf_size;   // 日志缓冲区大小
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;     // 每个日志文件最大行数

    // 获取当前时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    const char *p = strrchr(file_name, '/'); // 查找日志文件路径中的最后一个斜杠
    char log_full_name[512] = {0};

    // 如果路径中没有斜杠
    if (p == NULL)
    {
        snprintf(log_full_name, 511, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        strcpy(log_name, p + 1); // 提取日志文件名
        strncpy(dir_name, file_name, p - file_name + 1); // 提取日志文件路径
        snprintf(log_full_name, 511, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday; // 设置当前日期

    // 打开日志文件
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL)
    {
        return false;
    }

    return true;
}

/**
 * @brief 写入一条日志
 * @param level 日志级别
 * @param format 格式化字符串
 */
void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    // 根据日志级别设置前缀
    switch (level)
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    // 写入日志条数计数并判断是否需要切分文件
    m_mutex.lock();
    m_count++;

    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) // 每天一个日志文件或达到最大行数
    {
        char new_log[512] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[32] = {0};

        snprintf(tail, 32, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 511, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        else
        {
            snprintf(new_log, 511, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    // 写入时间和日志内容
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);

    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    // 如果是异步模式，将日志推入队列
    if (m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else
    {
        // 同步模式直接写入文件
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

/**
 * @brief 刷新日志到文件
 */
void Log::flush(void)
{
    m_mutex.lock();
    fflush(m_fp); // 强制刷新写入流缓冲区
    m_mutex.unlock();
}
