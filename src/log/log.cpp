/**
 * @file log.cpp
 * @brief 日志系统实现 - 支持同步/异步两种模式
 * 
 * 设计特点：
 * - 单例模式：全局唯一日志实例
 * - 双缓冲：减少I/O阻塞
 * - 日志分割：按日期或行数自动分割
 * - 异步写入：后台线程批量写入，提高性能
 * 
 * 核心库函数：
 * - time()/localtime(): 时间获取与格式化
 * - fopen()/fwrite()/fflush()/fclose(): 文件I/O
 * - gettimeofday(): 获取微秒级时间
 * - pthread_create(): 创建异步写入线程
 * - vsnprintf(): 可变参数格式化
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
 * 
 * @param file_name 日志文件名/路径（如 "./ServerLog"）
 * @param close_log 是否关闭日志（1=关闭，0=开启）
 * @param log_buf_size 单条日志缓冲区大小（字节）
 * @param split_lines 单文件最大行数（超过则分割）
 * @param max_queue_size 异步队列大小（0=同步模式，>0=异步模式）
 * @return true 初始化成功
 * @return false 初始化失败
 * 
 * 初始化流程：
 * 1. 判断是否启用异步模式（队列大小>0）
 * 2. 创建异步写入线程（如果异步）
 * 3. 分配日志缓冲区
 * 4. 根据日期构造日志文件名
 * 5. 打开日志文件
 * 
 * 库函数说明：
 * - time(): <time.h> 获取当前时间戳（秒）
 * - localtime(): <time.h> 将时间戳转换为本地时间结构
 *   * 返回 struct tm* 包含年月日时分秒
 *   * 注意：返回静态缓冲区，非线程安全
 * - strrchr(): <string.h> 查找字符最后出现位置
 * - snprintf(): <stdio.h> 安全的格式化字符串
 * - fopen(): <stdio.h> 打开文件
 *   * "a" 模式：追加写入，文件不存在则创建
 * - pthread_create(): <pthread.h> 创建线程
 */
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 如果设置了 max_queue_size，则启用异步日志模式
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        // 创建阻塞队列，用于存储待写入的日志
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // pthread_create(): 创建异步写日志的线程
        // 参数：线程ID指针、属性(NULL=默认)、入口函数、参数
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
 * 
 * @param level 日志级别（0=debug, 1=info, 2=warn, 3=error）
 * @param format 格式化字符串（printf风格）
 * @param ... 可变参数列表
 * 
 * 执行流程：
 * 1. 获取当前时间（微秒精度）
 * 2. 根据日志级别设置前缀
 * 3. 检查是否需要分割日志文件（日期变化或行数超限）
 * 4. 格式化日志内容
 * 5. 根据模式写入（同步直接写，异步放入队列）
 * 
 * 库函数说明：
 * - gettimeofday(): <sys/time.h> 获取微秒级时间
 *   * 参数1: struct timeval* {tv_sec秒, tv_usec微秒}
 *   * 参数2: 时区（通常传NULL）
 *   * 用于记录精确的日志时间戳
 * - va_list/va_start/va_end: <stdarg.h> 可变参数处理
 *   * va_start(): 初始化可变参数列表
 *   * va_end(): 清理可变参数列表
 * - vsnprintf(): <stdio.h> 可变参数格式化到缓冲区
 *   * 比snprintf多一个va_list参数
 *   * 返回写入的字符数（不含\0）
 * - fflush(): <stdio.h> 刷新文件缓冲区到磁盘
 * - fputs(): <stdio.h> 写入字符串到文件
 */
void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    // gettimeofday(): 获取当前时间（秒+微秒）
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    // localtime(): 将Unix时间戳转换为本地时间
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
