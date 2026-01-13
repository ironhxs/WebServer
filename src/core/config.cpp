/**
 * @file config.cpp
 * @brief 配置类实现 - 命令行参数解析
 */

#include "config.h"
#include <getopt.h>

using std::string;

/**
 * @brief 构造函数 - 初始化所有配置为默认值
 */
Config::Config(){
    // 服务器监听端口，默认9006
    PORT = 9006;

    // 日志写入方式：0=同步写入（每次写入立即刷新到磁盘），1=异步写入（先写入队列）
    LOGWrite = 0;

    // 触发组合模式：控制listenfd和connfd的触发模式
    // 0: LT + LT（水平触发，默认，稳定性好）
    // 1: LT + ET（监听用LT，连接用ET）
    // 2: ET + LT（监听用ET，连接用LT）
    // 3: ET + ET（边缘触发，性能最高但要求代码严谨）
    TRIGMode = 0;

    // 监听socket触发模式：0=LT（水平触发），1=ET（边缘触发）
    LISTENTrigmode = 0;

    // 连接socket触发模式：0=LT（水平触发），1=ET（边缘触发）
    CONNTrigmode = 0;

    // 优雅关闭连接：0=不使用，1=使用SO_LINGER选项
    // 使用后可确保数据完全发送再关闭连接
    OPT_LINGER = 0;

    // 数据库连接池大小，预先创建的MySQL连接数量
    // 根据并发量调整，一般设置为线程数相同或略大
    sql_num = 8;

    // 线程池工作线程数量
    // 建议设置为CPU核心数的1-2倍
    thread_num = 8;

    // 是否关闭日志：0=开启日志，1=关闭日志
    // 关闭日志可提升性能，但不利于调试和监控
    close_log = 0;

    // 并发模型：0=proactor（模拟），1=reactor
    // proactor：主线程完成读写，工作线程处理逻辑（推荐，更稳定）
    // reactor：工作线程完成读写和逻辑处理（有已知bug，大文件上传可能卡住）
    actor_model = 0;
}

/**
 * @brief 解析命令行参数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * 
 * 使用getopt()函数解析命令行参数
 * 示例：./server -p 8080 -l 1 -m 3 -t 16 -s 16
 */
void Config::parse_arg(int argc, char*argv[]){
    int opt;
    // 定义支持的参数选项，冒号表示该选项需要参数值
    const char *str = "p:l:m:o:s:t:c:a:";
    
    // 循环解析所有命令行参数
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':  // -p: 端口号
        {
            PORT = atoi(optarg);  // 将字符串转为整数
            break;
        }
        case 'l':  // -l: 日志写入方式
        {
            LOGWrite = atoi(optarg);
            break;
        }
        case 'm':  // -m: 触发模式
        {
            TRIGMode = atoi(optarg);
            break;
        }
        case 'o':  // -o: 优雅关闭
        {
            OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':  // -s: 数据库连接池数量
        {
            sql_num = atoi(optarg);
            break;
        }
        case 't':  // -t: 线程池线程数
        {
            thread_num = atoi(optarg);
            break;
        }
        case 'c':  // -c: 关闭日志
        {
            close_log = atoi(optarg);
            break;
        }
        case 'a':  // -a: 并发模型
        {
            actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}