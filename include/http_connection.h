/**
 * @file http_connection.h
 * @brief HTTP连接处理类定义
 * @details 负责请求解析、响应生成、静态/动态资源访问与连接状态管理。
 */

#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <atomic>
#include <time.h>

#include "thread_sync.h"
#include "database_pool.h"
#include "timer_list.h"
#include "log.h"

/**
 * @class http_conn
 * @brief 单个HTTP连接的处理类
 * @details 维护连接状态机、读写缓冲区、请求解析与响应构建，
 *          并结合定时器与epoll管理连接生命周期。
 */
class http_conn
{
public:
    static const int FILENAME_LEN = 200;               ///< 文件名最大长度
    static const int READ_BUFFER_SIZE = 10 * 1024 * 1024;  ///< 读缓冲区大小（10MB）
    static const int WRITE_BUFFER_SIZE = 8 * 1024;     ///< 写缓冲区大小
    /**
     * @brief HTTP方法枚举
     */
    enum METHOD
    {
        GET = 0,   ///< GET请求
        POST,      ///< POST请求
        HEAD,      ///< HEAD请求
        PUT,       ///< PUT请求
        DELETE,    ///< DELETE请求
        TRACE,     ///< TRACE请求
        OPTIONS,   ///< OPTIONS请求
        CONNECT,   ///< CONNECT请求
        PATH       ///< PATH请求
    };
    /**
     * @brief 请求解析状态
     */
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0, ///< 请求行解析
        CHECK_STATE_HEADER,          ///< 头部解析
        CHECK_STATE_CONTENT          ///< 内容解析
    };
    /**
     * @brief HTTP处理结果码
     */
    enum HTTP_CODE
    {
        NO_REQUEST,        ///< 请求不完整
        GET_REQUEST,       ///< 请求完整
        BAD_REQUEST,       ///< 语法错误
        NO_RESOURCE,       ///< 资源不存在
        FORBIDDEN_REQUEST, ///< 资源不可访问
        FILE_REQUEST,      ///< 静态文件请求
        DYNAMIC_REQUEST,   ///< 动态内容请求
        PHP_REQUEST,       ///< PHP处理请求
        INTERNAL_ERROR,    ///< 服务器内部错误
        CLOSED_CONNECTION  ///< 连接已关闭
    };
    /**
     * @brief 行解析状态
     */
    enum LINE_STATUS
    {
        LINE_OK = 0, ///< 行解析成功
        LINE_BAD,    ///< 行语法错误
        LINE_OPEN    ///< 行数据不完整
    };

public:
    /**
     * @brief 构造函数
     */
    http_conn() {}
    /**
     * @brief 析构函数
     */
    ~http_conn() {}

public:
    /**
     * @brief 初始化连接对象
     * @param sockfd 连接fd
     * @param addr 客户端地址
     * @param root 站点根目录
     * @param trigmode 触发模式
     * @param close_log 日志开关
     * @param user 数据库用户名
     * @param passwd 数据库密码
     * @param sqlname 数据库名
     */
    void init(int sockfd, const sockaddr_in &addr, char *root, int trigmode, int close_log, string user, string passwd, string sqlname);
    /**
     * @brief 关闭连接
     * @param real_close 是否真正关闭socket
     */
    void close_conn(bool real_close = true);
    /**
     * @brief 处理一次HTTP请求
     */
    void process();
    /**
     * @brief 读取数据到缓冲区
     * @return 是否读取成功
     */
    bool read_once();
    /**
     * @brief 写回响应数据
     * @return 是否写入成功
     */
    bool write();
    /**
     * @brief 获取客户端地址
     * @return 客户端地址指针
     */
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    /**
     * @brief 初始化数据库用户缓存
     * @param connPool 连接池指针
     */
    void initmysql_result(connection_pool *connPool);
    int timer_flag; ///< 计时器触发标记
    int improv;     ///< 处理完成标记
    /**
     * @brief 执行PHP脚本
     * @param script_path 脚本路径
     * @return 是否执行成功
     */
    bool execute_php(const char *script_path);


private:
    /**
     * @brief 重置连接状态
     */
    void init();
    /**
     * @brief 解析HTTP请求
     * @return 解析结果码
     */
    HTTP_CODE process_read();
    /**
     * @brief 构建HTTP响应
     * @param ret 请求处理结果
     * @return 是否构建成功
     */
    bool process_write(HTTP_CODE ret);
    /**
     * @brief 解析请求行
     * @param text 行内容
     * @return 解析结果码
     */
    HTTP_CODE parse_request_line(char *text);
    /**
     * @brief 解析请求头
     * @param text 头部内容
     * @return 解析结果码
     */
    HTTP_CODE parse_headers(char *text);
    /**
     * @brief 解析请求体
     * @param text 内容数据
     * @return 解析结果码
     */
    HTTP_CODE parse_content(char *text);
    /**
     * @brief 处理资源请求
     * @return 处理结果码
     */
    HTTP_CODE do_request();
    /**
     * @brief 生成状态JSON响应
     */
    HTTP_CODE handle_status_json();
    /**
     * @brief 处理欢迎页
     */
    HTTP_CODE handle_welcome_page();
    /**
     * @brief 处理上传请求
     */
    HTTP_CODE handle_upload_request();
    /**
     * @brief 获取上传列表
     */
    HTTP_CODE handle_upload_list();
    /**
     * @brief 删除上传文件
     */
    HTTP_CODE handle_upload_delete();
    /**
     * @brief 校验用户对上传文件的所有权
     */
    bool user_owns_upload(const std::string &owner, const std::string &stored_name) const;
    /**
     * @brief 处理用户图库页
     */
    HTTP_CODE handle_user_gallery_page();
    /**
     * @brief 处理用户视频页
     */
    HTTP_CODE handle_user_video_page();
    /**
     * @brief 更新客户端IP
     */
    void update_client_ip(const std::string &ip);
    /**
     * @brief 获取Cookie中的键值
     */
    std::string get_cookie_value(const std::string &key) const;
    /**
     * @brief 构建页面框架
     */
    std::string build_page_shell(const std::string &title, const std::string &body) const;
    /**
     * @brief 获取当前解析行
     */
    char *get_line() { return m_read_buf.data() + m_start_line; };
    /**
     * @brief 解析一行数据
     */
    LINE_STATUS parse_line();
    /**
     * @brief 释放内存映射
     */
    void unmap();
    /**
     * @brief 添加响应内容（格式化）
     */
    bool add_response(const char *format, ...);
    /**
     * @brief 添加响应体
     */
    bool add_content(const char *content);
    /**
     * @brief 添加状态行
     */
    bool add_status_line(int status, const char *title);
    /**
     * @brief 添加响应头部
     */
    bool add_headers(int content_length);
    /**
     * @brief 添加Content-Type头
     */
    bool add_content_type();
    /**
     * @brief 添加Content-Length头
     */
    bool add_content_length(int content_length);
    /**
     * @brief 添加Connection头
     */
    bool add_linger();
    /**
     * @brief 添加空行
     */
    bool add_blank_line();

public:
    static int m_epollfd; ///< 全局epoll实例fd
    static int m_user_count; ///< 当前连接数
    static locker m_ip_lock; ///< IP统计锁
    static std::unordered_map<std::string, int> m_ip_counts; ///< IP访问次数
    static std::unordered_set<std::string> m_unique_ips; ///< 唯一IP集合
    static std::atomic<long long> m_total_requests; ///< 总请求数
    static time_t m_start_time; ///< 统计起始时间
    MYSQL *mysql; ///< 当前连接使用的数据库指针
    int m_state;  ///< 当前读写状态（0=读，1=写）

private:
    int m_sockfd;                 ///< 连接fd
    char *m_php_content;          ///< PHP执行结果缓冲区
    size_t m_php_content_size;    ///< PHP内容大小
    sockaddr_in m_address;        ///< 客户端地址
    std::vector<char> m_read_buf; ///< 读缓冲区
    long m_read_idx;              ///< 读缓冲区已读索引
    long m_checked_idx;           ///< 已解析位置
    int m_start_line;             ///< 当前行起始位置
    char m_write_buf[WRITE_BUFFER_SIZE]; ///< 写缓冲区
    int m_write_idx;              ///< 写缓冲区索引
    CHECK_STATE m_check_state;    ///< 主状态机当前状态
    METHOD m_method;              ///< 请求方法
    char m_real_file[FILENAME_LEN]; ///< 请求文件路径
    char *m_url;                  ///< URL指针
    char *m_version;              ///< HTTP版本
    char *m_host;                 ///< Host头
    long m_content_length;        ///< 请求体长度
    bool m_linger;                ///< 是否保持长连接
    char *m_file_address;         ///< mmap文件地址
    bool m_is_mmap;               ///< 是否使用mmap
    std::string m_content_type;   ///< 响应Content-Type
    std::string m_cookie;         ///< Cookie内容
    std::string m_boundary;       ///< 上传边界分隔符
    std::string m_dynamic_content; ///< 动态内容
    std::string m_dynamic_content_type; ///< 动态内容类型
    std::string m_username;       ///< 当前用户名
    std::string m_extra_headers;  ///< 额外响应头
    bool m_ip_from_header;        ///< 是否从头部获取IP
    struct stat m_file_stat;      ///< 文件属性
    struct iovec m_iv[2];         ///< writev缓冲区
    int m_iv_count;               ///< iovec数量
    int cgi;                      ///< 是否启用POST处理
    char *m_string;               ///< 请求体缓存
    int bytes_to_send;            ///< 待发送字节数
    int bytes_have_send;          ///< 已发送字节数
    int m_response_status;        ///< 响应状态码
    char *doc_root;               ///< 网站根目录

    map<string, string> m_users;  ///< 用户名密码表
    int m_TRIGMode;               ///< epoll触发模式
    int m_close_log;              ///< 日志开关
    std::string m_ip;             ///< 客户端IP字符串

    char sql_user[100];           ///< 数据库用户名
    char sql_passwd[100];         ///< 数据库密码
    char sql_name[100];           ///< 数据库名
};

#endif
