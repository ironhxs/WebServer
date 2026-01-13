/**
 * @file main.cpp
 * @brief Web服务器主程序入口
 * @details 初始化并启动高性能Web服务器，支持HTTP请求处理、数据库连接池、
 *          线程池、异步日志系统等功能
 * @author hxs
 * @date 2026-01-09
 */

#include "webserver.h"
#include "config.h"

/**
 * @brief 主函数 - 服务器程序入口
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出状态码
 * 
 * 执行流程：
 * 1. 配置数据库连接参数
 * 2. 解析命令行参数
 * 3. 初始化服务器（端口、数据库、线程池等）
 * 4. 初始化日志系统
 * 5. 创建数据库连接池
 * 6. 创建线程池
 * 7. 设置触发模式（LT/ET）
 * 8. 开始监听端口
 * 9. 进入事件循环，处理客户端请求
 */
int main(int argc, char *argv[])
{
    // ========== 数据库配置 ==========
    string user = "root";              // 数据库用户名
    string passwd = "";                // 数据库密码（WSL Ubuntu MySQL默认root无密码）
    string databasename = "hxsdb";     // 使用的数据库名

    // ========== 命令行参数解析 ==========
    // 支持 -p 端口 -l 日志模式 -m 触发模式 -t 线程数 等参数
    Config config;
    config.parse_arg(argc, argv);

    // ========== 创建服务器对象 ==========
    WebServer server;

    // ========== 初始化服务器 ==========
    // 配置端口、数据库信息、日志、触发模式、连接池大小、线程数等
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    
    // ========== 初始化日志系统 ==========
    // 支持同步/异步写入日志文件，记录服务器运行状态
    server.log_write();

    // ========== 初始化数据库连接池 ==========
    // 预先创建多个数据库连接，避免每次请求都建立连接
    server.sql_pool();

    // ========== 初始化线程池 ==========
    // 预先创建工作线程，处理HTTP请求，避免频繁创建销毁线程
    server.thread_pool();

    // ========== 设置触发模式 ==========
    // 配置epoll的LT（水平触发）或ET（边缘触发）模式
    server.trig_mode();

    // ========== 开始监听端口 ==========
    // 创建监听socket，绑定端口，开始监听客户端连接
    server.eventListen();

    // ========== 进入事件循环 ==========
    // 使用epoll监听事件，处理客户端请求，永久循环直到服务器关闭
    server.eventLoop();

    return 0;
}
