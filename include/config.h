/**
 * @file config.h
 * @brief 服务器配置类 - 解析命令行参数并存储配置
 * @details 支持通过命令行参数自定义服务器运行配置
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <string>

/**
 * @class Config
 * @brief 配置管理类 - 负责解析和存储服务器配置参数
 * 
 * 支持的命令行参数：
 * -p: 端口号（默认9006）
 * -l: 日志写入方式（0=同步，1=异步）
 * -m: 触发模式（0=LT+LT, 1=LT+ET, 2=ET+LT, 3=ET+ET）
 * -o: 优雅关闭连接（0=不使用，1=使用）
 * -s: 数据库连接池数量（默认8）
 * -t: 线程池线程数量（默认8）
 * -c: 是否关闭日志（0=开启，1=关闭）
 * -a: 并发模型（0=proactor，1=reactor）
 */
class Config
{
public:
    /**
     * @brief 构造函数 - 初始化默认配置
     */
    Config();
    
    /**
     * @brief 析构函数
     */
    ~Config(){};

    /**
     * @brief 解析命令行参数
     * @param argc 参数个数
     * @param argv 参数数组
     * 
     * 使用示例：
     * ./server -p 8080 -l 1 -m 3 -t 16
     */
    void parse_arg(int argc, char*argv[]);

    // ========== 服务器配置参数 ==========
    
    int PORT;              ///< 监听端口号（默认9006）
    int LOGWrite;          ///< 日志写入方式：0=同步，1=异步（默认0）
    int TRIGMode;          ///< 触发组合模式：0~3（默认0=LT+LT）
    int LISTENTrigmode;    ///< 监听socket触发模式：0=LT，1=ET（默认0）
    int CONNTrigmode;      ///< 连接socket触发模式：0=LT，1=ET（默认0）
    int OPT_LINGER;        ///< 优雅关闭连接：0=不使用，1=使用（默认0）
    int sql_num;           ///< 数据库连接池大小（默认8）
    int thread_num;        ///< 线程池线程数量（默认8）
    int close_log;         ///< 是否关闭日志：0=开启，1=关闭（默认0）
    int actor_model;       ///< 并发模型：0=proactor，1=reactor（默认0）
};

#endif