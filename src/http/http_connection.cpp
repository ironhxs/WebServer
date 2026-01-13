/**
 * @file http_connection.cpp
 * @brief HTTP连接处理实现 - 本项目的核心文件
 * @details 实现请求解析、资源处理、响应构建以及辅助工具函数
 * 
 * ========== HTTP连接处理流程 ==========
 * 
 * 1. 连接初始化 (init)
 *    - 接收新连接的socket fd
 *    - 记录客户端IP地址
 *    - 将fd注册到epoll监听
 *    - 初始化读写缓冲区
 * 
 * 2. 读取请求 (read_once)
 *    - 从 socket 读取客户端发送的HTTP请求数据
 *    - 支持LT(水平触发)和ET(边缘触发)模式
 *    - 动态扩展缓冲区以支持大文件上传
 * 
 * 3. 解析请求 (process_read) - 有限状态机
 *    - CHECK_STATE_REQUESTLINE: 解析请求行 "GET /path HTTP/1.1"
 *    - CHECK_STATE_HEADER: 解析请求头 "Host: xxx\r\nConnection: keep-alive"
 *    - CHECK_STATE_CONTENT: 解析请求体 (仅POST请求)
 * 
 * 4. 处理请求 (do_request)
 *    - 解析URL，确定请求类型
 *    - 静态文件: 使用mmap映射文件内容
 *    - 动态请求: 登录/注册/状态/上传等
 *    - CGI处理: PHP脚本执行
 * 
 * 5. 构建响应 (process_write)
 *    - 根据处理结果构建HTTP响应
 *    - 状态行 + 响应头 + 响应体
 *    - 使用writev()散布写入提高效率
 * 
 * 6. 发送响应 (write)
 *    - 将响应数据发送给客户端
 *    - 支持大文件分片发送
 *    - 处理EAGAIN情况
 * 
 * 7. 连接关闭 (close_conn)
 *    - 从epoll移除监听
 *    - 关闭 socket
 *    - 更新连接计数
 * 
 * ========== 核心数据结构 ==========
 * 
 * 读缓冲区 (m_read_buf):
 *   存储客户端发送的原始HTTP请求数据
 *   包含: 请求行 + 请求头 + 空行 + 请求体
 * 
 * 写缓冲区 (m_write_buf):
 *   存储待发送的HTTP响应头部
 *   响应体通过mmap或动态内容单独处理
 * 
 * ========== 库函数依赖 ==========
 * 
 * 网络 I/O:
 *   - recv(): 接收数据
 *   - send(): 发送数据
 *   - writev(): 散布写入（多缓冲区一次性发送）
 * 
 * 文件操作:
 *   - open()/close(): 打开/关闭文件
 *   - stat(): 获取文件属性
 *   - mmap()/munmap(): 内存映射文件
 * 
 * epoll相关:
 *   - epoll_ctl(): 管理监听事件
 *   - fcntl(): 设置非阻塞模式
 * 
 * 数据库:
 *   - mysql_query(): 执行SQL查询
 *   - mysql_store_result(): 获取结果集
 */

#include "http_connection.h"



#include <mysql/mysql.h>

#include <algorithm>

#include <cctype>

#include <fstream>

#include <iomanip>

#include <sstream>

#include <vector>



/// 请求体最大允许大小（字节），限制上传文件不超过200MB
static const long kMaxBodySize = 200 * 1024 * 1024;


/**
 * HTTP状态码对应的提示文本
 * 
 * 常见HTTP状态码说明:
 * - 200 OK: 请求成功
 * - 400 Bad Request: 请求语法错误（如缺少必要字段）
 * - 403 Forbidden: 服务器拒绝请求（如无权限访问）
 * - 404 Not Found: 请求的资源不存在
 * - 500 Internal Error: 服务器内部错误
 */
const char *ok_200_title = "OK";

const char *error_400_title = "Bad Request";

const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";

const char *error_403_title = "Forbidden";

const char *error_403_form = "You do not have permission to get file form this server.\n";

const char *error_404_title = "Not Found";

const char *error_404_form = "The requested file was not found on this server.\n";

const char *error_500_title = "Internal Error";

const char *error_500_form = "There was an unusual problem serving the request file.\n";



/// 用户信息互斥锁
locker m_lock;

/// 内存中的用户表（用户名->密码）
map<string, string> users;



namespace

{

/**
 * @struct UploadItem
 * @brief 上传文件元数据
 */
struct UploadItem

{

    std::string stored_name; ///< 存储后的文件名

    std::string original_name; ///< 原始文件名

    long long size = 0; ///< 文件大小（字节）

    long long timestamp = 0; ///< 上传时间戳

};



/**
 * @brief 去除字符串首尾空白字符
 * @param value 原始字符串
 * @return 去除首尾空白后的字符串
 * 
 * 空白字符包括: 空格(' ')  制表符('\t') 换行('\n') 回车('\r') 等
 * 使用std::isspace()判断空白字符
 */
std::string trim(const std::string &value)

{
    // 从开头跳过空白
    size_t start = 0;

    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))

        ++start;
    // 从结尾跳过空白
    size_t end = value.size();

    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))

        --end;
    // 返回中间部分
    return value.substr(start, end - start);

}



/**
 * @brief 转换字符串为小写副本
 * @param value 原始字符串（传值传递）
 * @return 转换后的小写字符串
 * 
 * 用途: HTTP头字段名大小写不敏感的比较
 * 使用std::transform()和std::tolower()实现
 */
std::string to_lower_copy(std::string value)
{
    // transform将每个字符转为小写
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return value;
}

/**
 * @brief 将十六进制字符转换为数值
 * @param ch 十六进制字符 ('0'-'9', 'a'-'f', 'A'-'F')
 * @return 对应的数值(0-15)，无效字符返回-1
 * 
 * 用途: URL解码时将%XX转换为字节
 * 例如: hex_value('A') = 10, hex_value('f') = 15
 */
int hex_value(char ch)
{
    // 0-9 -> 0-9
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    // a-f -> 10-15
    if (ch >= 'a' && ch <= 'f')
        return 10 + (ch - 'a');
    // A-F -> 10-15
    if (ch >= 'A' && ch <= 'F')
        return 10 + (ch - 'A');
    return -1;  // 无效字符
}

/**
 * @brief URL解码（百分号编码解码）
 * @param value URL编码的字符串
 * @return 解码后的原始字符串
 * 
 * URL编码规则:
 *   - 空格(' ') 编码为 '+' 或 '%20'
 *   - 特殊字符 编码为 '%XX' (十六进制ASCII码)
 * 
 * 解码示例:
 *   "hello+world" -> "hello world"
 *   "hello%20world" -> "hello world"
 *   "%E4%B8%AD%E6%96%87" -> "中文" (UTF-8编码)
 */
std::string url_decode(const std::string &value)
{
    std::string out;
    out.reserve(value.size());  // 预分配内存
    for (size_t i = 0; i < value.size(); ++i)
    {
        // '+' 解码为空格
        if (value[i] == '+')
        {
            out.push_back(' ');
            continue;
        }
        // '%XX' 解码为对应字节
        if (value[i] == '%' && i + 2 < value.size())
        {
            int hi = hex_value(value[i + 1]);  // 高位
            int lo = hex_value(value[i + 2]);  // 低位
            if (hi >= 0 && lo >= 0)
            {
                // 组合成字节: hi*16 + lo
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;  // 跳过已处理的两个字符
                continue;
            }
        }
        // 普通字符直接复制
        out.push_back(value[i]);
    }
    return out;
}

/**
 * @brief 从表单请求体中提取指定键的值
 * @param body 表单请求体 (application/x-www-form-urlencoded格式)
 * @param key 要提取的键名
 * @return 解码后的值，未找到返回空字符串
 * 
 * 表单数据格式:
 *   key1=value1&key2=value2&key3=value3
 * 
 * 示例:
 *   body = "user=admin&password=123456"
 *   get_form_value(body, "user") 返回 "admin"
 *   get_form_value(body, "password") 返回 "123456"
 */
std::string get_form_value(const std::string &body, const std::string &key)
{
    // 构造搜索模式: "key="
    std::string pattern = key + "=";
    size_t pos = body.find(pattern);
    if (pos == std::string::npos)
        return "";
    // 提取值（到下一个&或字符串结尾）
    size_t start = pos + pattern.size();
    size_t end = body.find('&', start);
    std::string raw = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
    // URL解码后返回
    return url_decode(raw);
}

/**
 * @brief 从X-Forwarded-For头提取客户端IP
 * @param value X-Forwarded-For头的值
 * @return 提取到的客户端IP，失败返回空字符串
 * 
 * X-Forwarded-For说明:
 *   当请求经过代理服务器或CDN时，真实客户端IP
 *   会被放在此头中。
 * 
 *   格式: "client_ip, proxy1_ip, proxy2_ip"
 *   第一个IP即为原始客户端IP
 * 
 * 示例:
 *   "192.168.1.100, 10.0.0.1" -> 返回 "192.168.1.100"
 *   "192.168.1.100" -> 返回 "192.168.1.100"
 */
std::string extract_forwarded_ip(const std::string &value)
{
    std::string trimmed = trim(value);
    if (trimmed.empty())
        return "";
    size_t comma = trimmed.find(',');
    std::string ip = comma == std::string::npos ? trimmed : trimmed.substr(0, comma);
    return trim(ip);
}

/**
 * @brief HTML特殊字符转义
 * @param value 原始字符串
 * @return 转义后的安全HTML字符串
 * 
 * 转义规则:
 *   & -> &amp;   (必须最先转义)
 *   < -> &lt;
 *   > -> &gt;
 *   " -> &quot;
 *   ' -> &#39;
 * 
 * 用途: 防止XSS攻击，确保用户输入不会被浏览器解析为HTML
 * 
 * 示例:
 *   "<script>alert('xss')</script>"
 *   -> "&lt;script&gt;alert(&#39;xss&#39;)&lt;/script&gt;"
 */
std::string html_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size());

    for (char ch : value)

    {

        switch (ch)

        {

        case '&':

            out.append("&amp;");

            break;

        case '<':

            out.append("&lt;");

            break;

        case '>':

            out.append("&gt;");

            break;

        case '"':

            out.append("&quot;");

            break;

        default:

            out.push_back(ch);

            break;

        }

    }

    return out;

}



/**
 * @brief 规范化文件名，移除危险字符
 * @param value 原始文件名
 * @return 安全的文件名
 * 
 * 安全处理规则:
 *   1. 将路径分隔符替换为下划线: / \ :
 *   2. 将特殊字符替换为下划线: | < > "
 *   3. 将控制字符替换为下划线
 *   4. 移除开头的点号（防止隐藏文件）
 *   5. 空文件名默认为"upload.bin"
 * 
 * 用途: 防止目录穿越攻击和恶意文件名
 */
std::string sanitize_filename(const std::string &value)

{

    std::string name;

    name.reserve(value.size());
    // 过滤危险字符
    for (char ch : value)

    {
        // 路径分隔符和特殊字符替换为下划线
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '|' || ch == '<' || ch == '>' || ch == '"')

            name.push_back('_');
        // 控制字符替换为下划线
        else if (std::iscntrl(static_cast<unsigned char>(ch)))

            name.push_back('_');

        else

            name.push_back(ch);

    }
    // 移除开头的点号
    while (!name.empty() && name.front() == '.')

        name.erase(name.begin());
    // 空文件名使用默认名
    if (name.empty())

        name = "upload.bin";

    return name;

}



/**
 * @brief 格式化时间戳为可读字符串
 * @param timestamp Unix时间戳
 * @return 格式化的时间字符串 "YYYY-MM-DD HH:MM:SS"
 * 
 * 库函数说明:
 *   - localtime_r(): 线程安全的localtime
 *   - strftime(): 格式化时间到字符串
 *     * %Y: 4位年份
 *     * %m: 2位月份
 *     * %d: 2位日期
 *     * %H: 2位小时(24小时制)
 *     * %M: 2位分钟
 *     * %S: 2位秒
 */
std::string format_time(time_t timestamp)

{
    // 无效时间戳返回"-"
    if (timestamp <= 0)

        return "-";

    std::tm tm_snapshot{};

#if defined(_WIN32)

    localtime_s(&tm_snapshot, &timestamp);

#else

    localtime_r(&timestamp, &tm_snapshot);

#endif

    char buf[32];

    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_snapshot))

        return buf;

    return "-";

}



/**
 * @brief 根据HTTP状态码获取简短标题
 * @param status HTTP状态码
 * @return 状态码对应的英文描述
 * 
 * HTTP状态码分类:
 *   - 1xx: 信息性响应
 *   - 2xx: 成功响应
 *   - 3xx: 重定向响应
 *   - 4xx: 客户端错误
 *   - 5xx: 服务器错误
 */
const char *status_title(int status)

{

    switch (status)

    {

    case 200:
        return ok_200_title;        // OK

    case 302:
        return "Found";             // 临时重定向

    case 400:
        return error_400_title;     // Bad Request

    case 403:
        return error_403_title;     // Forbidden

    case 404:
        return error_404_title;     // Not Found

    case 413:
        return "Payload Too Large"; // 请求体过大

    case 500:

    default:
        return error_500_title;     // Internal Server Error

    }

}



/**
 * @brief 判断扩展名是否为图片格式
 * @param ext 小写的文件扩展名 (包含点号)
 * @return true为图片格式
 * 
 * 支持的图片格式:
 *   - .png: PNG图片
 *   - .jpg/.jpeg: JPEG图片
 *   - .gif: GIF动画
 *   - .webp: WebP图片
 *   - .svg: SVG矢量图
 */
bool is_image_ext(const std::string &ext)

{

    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".webp" || ext == ".svg";

}

/**
 * @brief 判断扩展名是否为视频格式
 * @param ext 小写的文件扩展名 (包含点号)
 * @return true为视频格式
 * 
 * 支持的视频格式:
 *   - .mp4: MP4视频 (H.264/H.265)
 *   - .webm: WebM视频 (VP8/VP9)
 *   - .ogg: Ogg视频 (Theora)
 */
bool is_video_ext(const std::string &ext)
{
    return ext == ".mp4" || ext == ".webm" || ext == ".ogg";
}

/**
 * @brief 从 sockaddr_in 结构提取IP地址字符串
 * @param addr 客户端地址结构
 * @return 点分十进制格式的IP地址
 * 
 * 库函数说明:
 *   - inet_ntop(): 将二进制IP转换为字符串
 *     * AF_INET: IPv4地址族
 *     * 参数: 地址族, 二进制地址, 输出缓冲区, 缓冲区大小
 *     * INET_ADDRSTRLEN: IPv4地址字符串最大长度(16)
 */
std::string ip_from_addr(const sockaddr_in &addr)
{
    char buf[INET_ADDRSTRLEN] = {0};
    // inet_ntop: network to presentation (binary -> string)
    const char *res = inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    if (!res)
        return "";
    return std::string(buf);
}

/**
 * @brief 判断是否为私有网络IPv4地址
 * @param ip 点分十进制格式的IP地址
 * @return true为私有地址
 * 
 * RFC1918私有地址范围:
 *   - 10.0.0.0/8: 10.x.x.x
 *   - 172.16.0.0/12: 172.16.x.x ~ 172.31.x.x
 *   - 192.168.0.0/16: 192.168.x.x
 * 
 * 另外 127.0.0.0/8 为本地回环地址
 */
bool is_private_ipv4(const std::string &ip)
{
    // 10.0.0.0/8 私有网络
    if (ip.rfind("10.", 0) == 0)
        return true;
    // 127.0.0.0/8 本地回环
    if (ip.rfind("127.", 0) == 0)
        return true;
    // 192.168.0.0/16 私有网络
    if (ip.rfind("192.168.", 0) == 0)
        return true;
    // 172.16.0.0/12 私有网络 (172.16.x.x ~ 172.31.x.x)
    if (ip.rfind("172.", 0) == 0)
    {
        size_t dot = ip.find('.', 4);
        if (dot != std::string::npos)
        {
            int second = std::atoi(ip.substr(4, dot - 4).c_str());
            if (second >= 16 && second <= 31)
                return true;
        }
    }
    return false;
}

/**
 * @brief 规范化客户端IP地址
 * @param ip 原始IP地址
 * @return 规范化后的IP，私有地址统一返回"local"
 * 
 * 用途:
 *   统一处理本地/内网访问，便于统计唯一访客数
 *   - ::1 (IPv6本地) -> "local"
 *   - 127.0.0.1 -> "local"
 *   - 192.168.x.x -> "local"
 *   - 10.x.x.x -> "local"
 */
std::string normalize_client_ip(const std::string &ip)
{
    if (ip.empty())
        return "";
    // IPv6本地地址
    if (ip == "::1")
        return "local";
    if (is_private_ipv4(ip))
        return "local";
    if (ip.rfind("fe80:", 0) == 0)
        return "local";
    return ip;
}

/**
 * @brief 从元数据文件加载用户上传列表
 * @param doc_root 网站根目录
 * @param username 用户名
 * @param items 输出参数，存储上传文件列表
 * @return true加载成功
 * 
 * 元数据文件格式:
 *   每行一条记录，字段用'|'分隔:
 *   存储名|原始名|大小|时间戳
 * 
 * 示例:
 *   admin_20240101120000_test.jpg|test.jpg|12345|1704067200
 */
bool load_user_uploads(const std::string &doc_root, const std::string &username, std::vector<UploadItem> &items)
{
    // 构建元数据文件路径
    const std::string meta_path = doc_root + "/uploads/.meta/" + username + ".list";

    std::ifstream meta(meta_path);

    if (!meta)

        return false;



    std::string line;

    while (std::getline(meta, line))

    {

        if (line.empty())

            continue;

        std::stringstream ss(line);

        UploadItem item;

        std::string size_str;

        std::string ts_str;

        if (!std::getline(ss, item.stored_name, '|'))

            continue;

        if (!std::getline(ss, item.original_name, '|'))

            continue;

        if (!std::getline(ss, size_str, '|'))

            continue;

        if (!std::getline(ss, ts_str))

            continue;

        try

        {

            item.size = std::stoll(size_str);

            item.timestamp = std::stoll(ts_str);

        }

        catch (...)

        {

            continue;

        }

        items.push_back(item);

    }

    return true;

}

} // namespace



/**
 * @brief 从数据库加载用户信息到内存
 * @param connPool 数据库连接池指针
 * 
 * 功能说明:
 *   在服务器启动时调用，将数据库中的用户表加载到内存中。
 *   这样登录验证时无需每次都查询数据库，提高性能。
 * 
 * 数据结构:
 *   users: map<string, string> - 用户名到密码的映射
 * 
 * MySQL C API说明:
 *   - mysql_query(): 执行SQL语句
 *     * 参数: MYSQL连接句柄, SQL字符串
 *     * 返回: 0成功, 非0失败
 * 
 *   - mysql_store_result(): 获取查询结果集
 *     * 将结果全部读入内存
 *     * 适合结果集较小的情况
 *     * 返回: MYSQL_RES*结果集指针
 * 
 *   - mysql_fetch_row(): 获取下一行数据
 *     * 返回: MYSQL_ROW (字符串数组)
 *     * 没有更多数据时返回NULL
 * 
 *   - mysql_free_result(): 释放结果集内存
 *     * 必须调用，否则内存泄漏
 * 
 *   - mysql_error(): 获取错误信息
 *     * 返回最近一次操作的错误描述
 * 
 * 示例SQL:
 *   SELECT username, passwd FROM user;
 *   返回所有用户的用户名和密码
 */
void http_conn::initmysql_result(connection_pool *connPool)
{
    // 使用RAII获取数据库连接，函数结束时自动释放
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 执行SQL查询：获取所有用户
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
        return;
    }

    // 获取查询结果集
    MYSQL_RES *result = mysql_store_result(mysql);
    if (!result)
        return;

    // 遍历结果集，将用户信息加载到内存map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);  // 用户名
        string temp2(row[1]);  // 密码
        users[temp1] = temp2;  // 存入map
    }
    
    // 释放结果集内存
    mysql_free_result(result);
}



/**
 * @brief 设置文件描述符为非阻塞模式
 * 
 * @param fd 文件描述符
 * @return int 原有的文件状态标志
 * 
 * 非阻塞I/O说明：
 * - 阻塞模式：read/write会等待直到操作完成
 * - 非阻塞模式：立即返回，无数据时返回-1，errno=EAGAIN
 * 
 * 库函数说明：
 * - fcntl(): <fcntl.h> 文件控制操作
 *   * F_GETFL: 获取文件状态标志
 *   * F_SETFL: 设置文件状态标志
 *   * O_NONBLOCK: 非阻塞标志
 *   * 成功返回0，失败返回-1
 */
int setnonblocking(int fd)

{
    // fcntl(fd, F_GETFL): 获取当前文件状态标志
    int old_option = fcntl(fd, F_GETFL);
    // 添加非阻塞标志
    int new_option = old_option | O_NONBLOCK;
    // fcntl(fd, F_SETFL): 设置新的文件状态标志
    fcntl(fd, F_SETFL, new_option);

    return old_option;

}



/**
 * @brief 将文件描述符注册到epoll实例
 * 
 * @param epollfd epoll实例描述符
 * @param fd 待注册的文件描述符
 * @param one_shot 是否启用EPOLLONESHOT（防止多线程同时处理同一连接）
 * @param TRIGMode 触发模式（0=LT水平触发, 1=ET边缘触发）
 * 
 * 事件标志说明：
 * - EPOLLIN: 可读事件（有数据到达）
 * - EPOLLET: 边缘触发模式
 * - EPOLLRDHUP: 对端关闭连接或半关闭
 * - EPOLLONESHOT: 事件只触发一次，处理完需重新注册
 * 
 * 库函数说明：
 * - epoll_ctl(): <sys/epoll.h> epoll控制操作
 *   * 参数1: epoll实例描述符
 *   * 参数2: 操作类型（EPOLL_CTL_ADD/MOD/DEL）
 *   * 参数3: 目标文件描述符
 *   * 参数4: epoll_event结构指针
 *   * 成功返回0，失败返回-1
 */
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)

{

    epoll_event event;

    event.data.fd = fd;


    // 根据触发模式设置事件标志
    if (1 == TRIGMode)
        // ET模式：边缘触发，需要一次性读完所有数据
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    else
        // LT模式：水平触发，有数据就会一直触发
        event.events = EPOLLIN | EPOLLRDHUP;


    // EPOLLONESHOT: 确保一个socket只被一个线程处理
    if (one_shot)

        event.events |= EPOLLONESHOT;
    // epoll_ctl(): 将fd添加到epoll监听集合
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    // 设置为非阻塞，配合epoll使用
    setnonblocking(fd);

}



/**
 * @brief 从epoll移除文件描述符并关闭连接
 * 
 * @param epollfd epoll实例描述符
 * @param fd 待移除的文件描述符
 * 
 * 库函数说明：
 * - epoll_ctl(EPOLL_CTL_DEL): 从epoll中删除fd
 * - close(): <unistd.h> 关闭文件描述符
 *   * 释放内核中的文件描述符资源
 *   * 对于socket，会触发TCP四次挥手
 */
void removefd(int epollfd, int fd)

{
    // 从epoll监听集合中删除
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    // 关闭socket，释放资源
    close(fd);

}



/**
 * @brief 修改文件描述符在epoll中的监听事件
 * 
 * @param epollfd epoll实例描述符
 * @param fd 目标文件描述符
 * @param ev 新的事件类型（EPOLLIN读/EPOLLOUT写）
 * @param TRIGMode 触发模式
 * 
 * 使用场景：
 * - 读取请求完成后，改为监听写事件（准备发送响应）
 * - 写入响应完成后，改为监听读事件（等待下一个请求）
 * - EPOLLONESHOT事件处理完后需要重新注册
 * 
 * 库函数说明：
 * - epoll_ctl(EPOLL_CTL_MOD): 修改已注册fd的事件
 */
void modfd(int epollfd, int fd, int ev, int TRIGMode)

{

    epoll_event event;

    event.data.fd = fd;


    // 根据触发模式设置事件标志
    if (1 == TRIGMode)
        // ET模式 + ONESHOT + 对端关闭检测
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

    else

        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;


    // epoll_ctl(): 修改fd的监听事件
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);

}



// Static members.

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;
locker http_conn::m_ip_lock;
std::unordered_map<std::string, int> http_conn::m_ip_counts;
std::unordered_set<std::string> http_conn::m_unique_ips;
std::atomic<long long> http_conn::m_total_requests{0};
time_t http_conn::m_start_time = 0;


/**
 * @brief 关闭客户端连接并清理资源
 * @param real_close 是否真正关闭连接
 * 
 * 功能说明:
 *   关闭连接时执行以下清理工作:
 *   1. 更新IP连接计数（用于统计在线用户）
 *   2. 从epoll移除监听
 *   3. 关閭socket文件描述符
 *   4. 减少全局连接计数
 */
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        // printf("close %d\n", m_sockfd);  // Debug output removed for performance
        // 更新IP连接统计（用于在线用户计数）
        if (!m_ip.empty())
        {
            m_ip_lock.lock();  // 加锁保护共享数据
            auto it = m_ip_counts.find(m_ip);
            if (it != m_ip_counts.end())
            {
                if (it->second <= 1)
                    m_ip_counts.erase(it);  // 该IP最后一个连接关闭
                else
                    --(it->second);         // 该IP还有其他连接
            }
            m_ip_lock.unlock();
        }
        // removefd(): 从epoll移除并关閭socket
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;  // 减少全局连接计数
    }
}


/**
 * @brief 初始化HTTP连接（带客户端信息）
 * @param sockfd 客户端socket文件描述符
 * @param addr 客户端地址结构
 * @param root 网站根目录
 * @param TRIGMode epoll触发模式 (0=LT, 1=ET)
 * @param close_log 是否关闭日志
 * @param user MySQL用户名
 * @param passwd MySQL密码
 * @param sqlname MySQL数据库名
 * 
 * 功能说明:
 *   当新连接建立时调用，执行以下操作:
 *   1. 保存socket和客户端地址
 *   2. 解析并规范化客户端IP
 *   3. 更新IP统计信息
 *   4. 注册到epoll监听
 *   5. 保存服务器配置
 *   6. 初始化内部状态
 */
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;            // 保存socket描述符
    m_address = addr;             // 保存客户端地址
    // 解析并规范化IP地址
    m_ip = normalize_client_ip(ip_from_addr(addr));
    m_ip_from_header = false;
    // NOTE:更新IP连接统计
    if (!m_ip.empty())
    {
        m_ip_lock.lock();
        ++m_ip_counts[m_ip];        // 该IP连接数+1
        m_unique_ips.insert(m_ip);  // 记录唯一访客
        m_ip_lock.unlock();
    }

    // 注册socket到epoll，启用ONESHOT防止多线程同时处理
    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;  // 全局连接数+1


    // Store server config for this connection.
    // 保存网站根目录
    doc_root = root;
    // 保存触发模式和日志配置
    m_TRIGMode = TRIGMode;

    m_close_log = close_log;



    // Cache DB credentials for later use.
    // 缓存数据库认证信息，用于后续注册/登录操作
    strcpy(sql_user, user.c_str());

    strcpy(sql_passwd, passwd.c_str());

    strcpy(sql_name, sqlname.c_str());


    // 初始化内部状态
    init();
}

/**
 * @brief 更新客户端IP（从HTTP头获取真实IP时调用）
 * @param ip 新的IP地址
 * 
 * 用途:
 *   当服务器在Cloudflare/Nginx代理后面时，需要从HTTP头
 *   (X-Forwarded-For, CF-Connecting-IP)获取真实客户端IP
 */
void http_conn::update_client_ip(const std::string &ip)
{
    std::string normalized = normalize_client_ip(ip);
    if (normalized.empty() || normalized == m_ip)
        return;
    // 更新IP统计需要加锁
    m_ip_lock.lock();
    // 减少旧IP的连接计数
    if (!m_ip.empty())
    {
        auto it = m_ip_counts.find(m_ip);
        if (it != m_ip_counts.end())
        {
            if (it->second <= 1)
                m_ip_counts.erase(it);
            else
                --(it->second);
        }
    }
    // 更新为新IP
    m_ip = normalized;
    ++m_ip_counts[m_ip];
    m_unique_ips.insert(m_ip);
    m_ip_lock.unlock();
    m_ip_from_header = true;  // 标记IP来自HTTP头
}

/**
 * @brief 初始化/重置HTTP连接内部状态
 * 
 * 调用时机:
 *   1. 新连接建立后
 *   2. keep-alive连接处理完一个请求后，准备处理下一个
 * 
 * 重置的状态包括:
 *   - 状态机状态 (CHECK_STATE_REQUESTLINE)
 *   - 缓冲区索引
 *   - 文件映射
 *   - 动态内容
 *   - Cookie和用户名
 */
void http_conn::init()
{
    // 记录服务器启动时间（仅首次）
    if (m_start_time == 0)
        m_start_time = time(nullptr);
    mysql = NULL;

    m_php_content = NULL;

    m_php_content_size = 0;

    bytes_to_send = 0;

    bytes_have_send = 0;
    // 状态机初始状态：等待解析请求行
    m_check_state = CHECK_STATE_REQUESTLINE;
    // 默认短连接
    m_linger = false;
    // 默认GET请求
    m_method = GET;

    m_url = 0;

    m_version = 0;

    m_content_length = 0;

    m_host = 0;

    m_start_line = 0;

    m_checked_idx = 0;

    m_read_idx = 0;

    m_write_idx = 0;

    m_file_address = NULL;

    m_is_mmap = false;

    m_content_type = "text/html; charset=utf-8";

    m_cookie.clear();

    m_boundary.clear();

    m_dynamic_content.clear();

    m_dynamic_content_type.clear();

    m_username.clear();

    m_extra_headers.clear();

    m_response_status = 200;

    cgi = 0;

    m_state = 0;

    timer_flag = 0;

    improv = 0;



    m_read_buf.assign(READ_BUFFER_SIZE, '\0');
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);

    memset(m_real_file, '\0', FILENAME_LEN);

}



/**
 * @brief 解析读缓冲区中的一行数据
 * @return LINE_STATUS 行解析结果
 * 
 * HTTP协议行格式:
 *   每行以 \r\n (CRLF) 结束
 *   例如: "GET /index.html HTTP/1.1\r\n"
 * 
 * 返回值说明:
 *   - LINE_OK: 成功解析完整一行
 *   - LINE_OPEN: 数据不完整，需要继续读取
 *   - LINE_BAD: 行格式错误
 * 
 * 工作原理:
 *   遍历缓冲区，查找 \r\n 序列
 *   找到后将其替换为 \0\0 作为字符串结束符
 */
http_conn::LINE_STATUS http_conn::parse_line()

{

    char temp;
    // 从当前检查位置继续扫描
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)

    {

        temp = m_read_buf[m_checked_idx];
        // 检测到\r字符
        if (temp == '\r')

        {
            // \r是最后一个字符，还需要读取\n
            if ((m_checked_idx + 1) == m_read_idx)

                return LINE_OPEN;
            // \r后紧跟\n，完整的一行
            else if (m_read_buf[m_checked_idx + 1] == '\n')

            {
                // 将\r\n替换为\0\0
                m_read_buf[m_checked_idx++] = '\0';

                m_read_buf[m_checked_idx++] = '\0';

                return LINE_OK;

            }
            // \r后不是\n，格式错误
            return LINE_BAD;

        }
        // 检测到\n字符（某些客户端可能先发\n）
        else if (temp == '\n')

        {
            // 如果前一个字符是\r，也算完整
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')

            {

                m_read_buf[m_checked_idx - 1] = '\0';

                m_read_buf[m_checked_idx++] = '\0';

                return LINE_OK;

            }

            return LINE_BAD;

        }

    }
    // 遍历完毕但未找到行结束符
    return LINE_OPEN;

}


/**
 * @brief 从socket读取客户端数据（一次性读取）
 * 
 * @return true 读取成功（可能部分读取）
 * @return false 读取失败或连接关闭
 * 
 * 读取策略：
 * - LT模式：循环读取直到EAGAIN
 * - ET模式：必须一次性读完所有数据
 * - 支持动态扩展缓冲区以处理大文件上传
 * 
 * 库函数说明：
 * - recv(): <sys/socket.h> 从socket接收数据
 *   * 参数1: socket描述符
 *   * 参数2: 接收缓冲区指针
 *   * 参数3: 缓冲区大小
 *   * 参数4: 标志位（0=默认阻塞读取）
 *   * 返回值:
 *     - >0: 实际读取的字节数
 *     - =0: 对端关闭连接（收到FIN）
 *     - <0: 出错，检查errno
 *   * errno说明:
 *     - EAGAIN/EWOULDBLOCK: 非阻塞模式下暂无数据
 *     - EINTR: 被信号中断，应重试
 *     - ECONNRESET: 连接被对端重置
 */
bool http_conn::read_once()

{

    auto grow_read_buffer = [&]() -> bool {
        size_t current = m_read_buf.size();
        size_t max_size = static_cast<size_t>(kMaxBodySize) + 4096;
        if (current >= max_size)
            return false;
        size_t next = current * 2;
        if (next < current + 4096)
            next = current + 4096;
        if (next > max_size)
            next = max_size;
        m_read_buf.resize(next, '\0');
        return true;
    };

    if (m_read_idx >= static_cast<long>(m_read_buf.size()))
    {
        if (!grow_read_buffer())
            return false;
    }
    int bytes_read = 0;


    // LT mode read - 改为循环读取以支持大文件上传
    if (0 == m_TRIGMode)
    {
        // LT模式也需要循环读取，直到EAGAIN或读完
        long total_read = 0;
        while (true)
        {
            if (m_read_idx >= static_cast<long>(m_read_buf.size()))
            {
                if (!grow_read_buffer())
                {
                    LOG_ERROR("LT read: grow_read_buffer failed, m_read_idx=%ld", m_read_idx);
                    return false;
                }
            }
            bytes_read = recv(m_sockfd, m_read_buf.data() + m_read_idx,
                              static_cast<int>(m_read_buf.size() - m_read_idx), 0);
            if (bytes_read < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    LOG_INFO("LT read: EAGAIN, total_read=%ld, m_read_idx=%ld", total_read, m_read_idx);
                    break;  // 数据暂时读完，返回成功
                }
                if (errno == EINTR)
                    continue;  // 被信号中断，继续读
                LOG_ERROR("LT read: recv error, errno=%d", errno);
                return false;  // 真正的错误
            }
            if (bytes_read == 0)
            {
                LOG_INFO("LT read: connection closed by peer, total_read=%ld", total_read);
                return false;  // 连接关闭
            }
            m_read_idx += bytes_read;
            total_read += bytes_read;
        }
        LOG_INFO("LT read success: total_read=%ld, m_read_idx=%ld, content_length=%ld", 
                 total_read, m_read_idx, m_content_length);
        return true;
    }

    // ET mode read.

    else

    {

        while (true)

        {

            if (m_read_idx >= static_cast<long>(m_read_buf.size()))
            {
                if (!grow_read_buffer())
                    return false;
            }
            bytes_read = recv(m_sockfd, m_read_buf.data() + m_read_idx,
                              static_cast<int>(m_read_buf.size() - m_read_idx), 0);
            if (bytes_read == -1)

            {

                if (errno == EAGAIN || errno == EWOULDBLOCK)

                    break;
                if (errno == EINTR)

                    continue;
                return false;

            }

            else if (bytes_read == 0)

            {

                return false;

            }

            m_read_idx += bytes_read;

        }

        return true;

    }

}



/**
 * @brief 解析HTTP请求行
 * @param text 请求行字符串
 * @return HTTP_CODE 解析结果
 * 
 * HTTP请求行格式:
 *   METHOD SP REQUEST-URI SP HTTP-VERSION CRLF
 *   例如: "GET /index.html HTTP/1.1"
 * 
 * 解析过程:
 *   1. 查找第一个空格/Tab，分离方法
 *   2. 验证方法是否为GET或POST
 *   3. 查找第二个空格/Tab，分离URL
 *   4. 验证HTTP版本是否为1.1
 *   5. 处理URL中的协议前缀
 *   6. 状态转换到解析请求头
 * 
 * 库函数说明:
 *   - strpbrk(): 查找字符串中第一个属于指定字符集的字符
 *   - strspn(): 计算字符串开头连续包含指定字符集的长度
 *   - strcasecmp(): 忽略大小写比较字符串
 *   - strncasecmp(): 忽略大小写比较指定长度
 *   - strchr(): 查找字符第一次出现的位置
 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)

{
    // strpbrk(): 查找第一个空格或Tab
    m_url = strpbrk(text, " \t");

    if (!m_url)

    {

        return BAD_REQUEST;

    }
    // 将空格替换为\0，分离方法字符串
    *m_url++ = '\0';

    char *method = text;
    // strcasecmp(): 忽略大小写比较
    if (strcasecmp(method, "GET") == 0)

        m_method = GET;

    else if (strcasecmp(method, "POST") == 0)

    {

        m_method = POST;

        cgi = 1;  // POST请求需要处理请求体

    }

    else

        return BAD_REQUEST;
    // strspn(): 跳过连续的空格/Tab
    m_url += strspn(m_url, " \t");
    // 查找 URL 和 HTTP 版本的分隔符
    m_version = strpbrk(m_url, " \t");

    if (!m_version)

        return BAD_REQUEST;

    *m_version++ = '\0';

    m_version += strspn(m_version, " \t");
    // 只支持HTTP/1.1
    if (strcasecmp(m_version, "HTTP/1.1") != 0)

        return BAD_REQUEST;
    // 处理带协议前缀的URL: http://host/path
    if (strncasecmp(m_url, "http://", 7) == 0)

    {

        m_url += 7;

        m_url = strchr(m_url, '/');

    }

    // 处理https前缀
    if (strncasecmp(m_url, "https://", 8) == 0)

    {

        m_url += 8;

        m_url = strchr(m_url, '/');

    }

    // URL必须以/开头
    if (!m_url || m_url[0] != '/')

        return BAD_REQUEST;

    // Default to index.html for root.
    // 根路径默认重定向到index.html
    if (strlen(m_url) == 1)

        strcat(m_url, "index.html");
    // 状态机转换:请求行 -> 请求头
    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;

}



/**
 * @brief 解析HTTP请求头
 * @param text 请求头行字符串
 * @return HTTP_CODE 解析结果
 * 
 * HTTP请求头格式:
 *   Header-Name: Header-Value CRLF
 *   例如: "Host: www.example.com\r\n"
 *         "Content-Length: 1234\r\n"
 *         "Connection: keep-alive\r\n"
 * 
 * 支持的请求头:
 *   - Connection: 连接类型 (keep-alive/close)
 *   - Content-Length: 请求体长度
 *   - Content-Type: 内容类型，含文件上传boundary
 *   - Host: 目标主机
 *   - Cookie: 用户登录信息
 *   - Expect: 100-continue处理
 *   - X-Forwarded-For: 代理/CDN的真实IP
 *   - CF-Connecting-IP: Cloudflare的真实IP
 * 
 * 特殊处理:
 *   - 空行表示请求头结束
 *   - Expect: 100-continue需发送继续响应
 *   - 大文件上传需动态扩展缓冲区
 */
http_conn::HTTP_CODE http_conn::parse_headers(char *text)

{
    // 空行表示请求头结束
    if (text[0] == '\0')

    {
        // 有请求体时转到解析请求体状态
        if (m_content_length != 0)

        {

            m_check_state = CHECK_STATE_CONTENT;

            return NO_REQUEST;

        }
        // 无请求体，请求解析完成
        return GET_REQUEST;

    }
    // 解析Connection头
    else if (strncasecmp(text, "Connection:", 11) == 0)

    {

        text += 11;

        text += strspn(text, " \t");
        // keep-alive表示长连接
        if (strcasecmp(text, "keep-alive") == 0)

        {

            m_linger = true;

        }

    }
    // 解析Content-Length头
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
        if (m_content_length > kMaxBodySize)
        {
            m_response_status = 413;

            m_dynamic_content_type = "text/html; charset=utf-8";

            m_dynamic_content = build_page_shell(

                "&#x8bf7;&#x6c42;&#x4f53;&#x8fc7;&#x5927;",

                R"HTML(<section class="panel" style="max-width: 620px; margin: 0 auto;">

<h2 style="font-size: 24px;">&#x4e0a;&#x4f20;&#x5931;&#x8d25;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x8bf7;&#x6c42;&#x4f53;&#x8d85;&#x8fc7;&#x670d;&#x52a1;&#x5668;&#x9650;&#x5236;&#xff0c;&#x8bf7;&#x7f29;&#x5c0f;&#x6587;&#x4ef6;&#x540e;&#x518d;&#x8bd5;&#x3002;</p>

<div class="actions" style="margin-top: 16px;">

<a class="btn primary" href="/pages/upload.html">&#x8fd4;&#x56de;&#x4e0a;&#x4f20;</a>


</div>

</section>)HTML");

            return DYNAMIC_REQUEST;
        }
        size_t needed = std::min(static_cast<size_t>(m_content_length) + 4096,
                                 static_cast<size_t>(READ_BUFFER_SIZE) * 2);
        if (needed > m_read_buf.size())
            m_read_buf.resize(needed, '\0');
    }
    else if (strncasecmp(text, "Expect:", 7) == 0)
    {
        text += 7;
        text += strspn(text, " \t");
        std::string lower = to_lower_copy(text);
        if (lower.find("100-continue") != std::string::npos)
        {
            const char continue_msg[] = "HTTP/1.1 100 Continue\r\n\r\n";
            send(m_sockfd, continue_msg, sizeof(continue_msg) - 1, 0);
        }
    }
    else if (strncasecmp(text, "Content-Type:", 13) == 0)
    {
        text += 13;
        text += strspn(text, " 	");
        std::string value = text;
        std::string lower = to_lower_copy(value);
        size_t pos = lower.find("boundary=");
        if (pos != std::string::npos)
        {
            size_t start = pos + 9;
            size_t end = value.find(';', start);
            std::string boundary = trim(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (boundary.size() >= 2 && boundary.front() == '"' && boundary.back() == '"')
                boundary = boundary.substr(1, boundary.size() - 2);
            m_boundary = boundary;
        }
    }
    else if (strncasecmp(text, "Host:", 5) == 0)

    {

        text += 5;

        text += strspn(text, " \t");

        m_host = text;

    }

    else if (strncasecmp(text, "Cookie:", 7) == 0)
    {
        text += 7;
        text += strspn(text, " \t");
        m_cookie = text;
    }
    else if (strncasecmp(text, "X-Forwarded-For:", 16) == 0)
    {
        text += 16;
        text += strspn(text, " \t");
        update_client_ip(extract_forwarded_ip(text));
    }
    else if (strncasecmp(text, "CF-Connecting-IP:", 17) == 0)
    {
        text += 17;
        text += strspn(text, " \t");
        update_client_ip(extract_forwarded_ip(text));
    }
    else
    {

        LOG_INFO("oop!unknow header: %s", text);

    }

    return NO_REQUEST;

}



/**
 * @brief 解析HTTP请求体
 * @param text 请求体起始指针
 * @return HTTP_CODE 解析结果
 * 
 * 工作原理:
 *   检查是否已接收完整的请求体数据:
 *   - m_start_line: 请求体在缓冲区中的起始位置
 *   - m_content_length: 通过Content-Length头获取的请求体长度
 *   - m_read_idx: 已读取的总字节数
 * 
 * 请求体类型:
 *   - 表单数据: application/x-www-form-urlencoded
 *     格式: key1=value1&key2=value2
 * 
 *   - 文件上传: multipart/form-data
 *     格式: --boundary\r\n
 *           Content-Disposition: form-data; name="file"; filename="xx"\r\n
 *           \r\n
 *           <文件二进制内容>
 *           --boundary--\r\n
 */
http_conn::HTTP_CODE http_conn::parse_content(char *text)

{
    // 检查是否收到了完整的请求体
    // m_start_line 是请求体的起始位置（头部结束后）
    // m_content_length 是请求体的长度
    // m_read_idx 是已读取的总字节数
    long needed = m_start_line + m_content_length;
    if (m_read_idx >= needed)

    {

        text[m_content_length] = '\0';

        m_string = text;

        return GET_REQUEST;

    }

    return NO_REQUEST;

}



/**
 * @brief HTTP请求解析主状态机
 * @return HTTP_CODE 解析结果
 * 
 * 状态机说明:
 *   三个主状态顺序转换:
 *   CHECK_STATE_REQUESTLINE -> CHECK_STATE_HEADER -> CHECK_STATE_CONTENT
 * 
 *   状态转换触发条件:
 *   - REQUESTLINE -> HEADER: 请求行解析成功
 *   - HEADER -> CONTENT: 遇到空行且Content-Length>0
 *   - HEADER -> 完成: 遇到空行且Content-Length=0(GET请求)
 * 
 * 返回值说明:
 *   - NO_REQUEST: 请求不完整，需要更多数据
 *   - GET_REQUEST: GET请求解析完成
 *   - BAD_REQUEST: 请求格式错误
 *   - DYNAMIC_REQUEST: 动态内容请求
 *   - FILE_REQUEST: 文件请求
 *   - INTERNAL_ERROR: 内部错误
 */
http_conn::HTTP_CODE http_conn::process_read()

{

    LINE_STATUS line_status = LINE_OK;

    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;

    // 特殊处理: 请求体不需要逐行解析
    // 处理请求体时的特殊逻辑：不需要逐行解析
    if (m_check_state == CHECK_STATE_CONTENT && m_content_length > 0)
    {
        // 直接检查是否收到了完整的请求体
        char *text = m_read_buf.data() + m_start_line;
        HTTP_CODE ret = parse_content(text);
        if (ret == GET_REQUEST)
            return do_request();
        return NO_REQUEST;  // 还需要更多数据
    }
    // 主循环: 逐行解析请求行和请求头
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))

    {
        // 获取当前行
        text = get_line();
        // 记录下一行的起始位置
        m_start_line = m_checked_idx;

        if (m_check_state != CHECK_STATE_CONTENT)
        {
            LOG_INFO("%s", text);
        }
        // 根据当前状态处理
        switch (m_check_state)

        {
        // 状态一: 解析请求行
        case CHECK_STATE_REQUESTLINE:

        {

            ret = parse_request_line(text);

            if (ret == BAD_REQUEST)

                return BAD_REQUEST;

            break;

        }
        // 状态二: 解析请求头
        case CHECK_STATE_HEADER:

        {

            ret = parse_headers(text);

            if (ret == BAD_REQUEST)

                return BAD_REQUEST;

            if (ret == DYNAMIC_REQUEST)

                return DYNAMIC_REQUEST;
            // GET请求完成，开始处理
            if (ret == GET_REQUEST)

                return do_request();

            break;

        }
        // 状态三: 解析请求体
        case CHECK_STATE_CONTENT:

        {

            ret = parse_content(text);
            // POST请求完成，开始处理
            if (ret == GET_REQUEST)

                return do_request();

            line_status = LINE_OPEN;

            break;

        }

        default:

            return INTERNAL_ERROR;

        }

    }
    // 数据不完整，需要继续读取
    return NO_REQUEST;

}


/**
 * @brief 从Cookie字符串中获取指定键的值
 * @param key Cookie键名
 * @return Cookie值，未找到返回空字符串
 * 
 * Cookie格式:
 *   "key1=value1; key2=value2; key3=value3"
 * 
 * 示例:
 *   Cookie: "ws_user=admin; session_id=abc123"
 *   get_cookie_value("ws_user") 返回 "admin"
 */
std::string http_conn::get_cookie_value(const std::string &key) const

{

    if (m_cookie.empty() || key.empty())

        return "";

    size_t pos = 0;

    while (pos < m_cookie.size())

    {

        size_t end = m_cookie.find(';', pos);

        std::string pair = trim(m_cookie.substr(pos, end - pos));

        size_t eq = pair.find('=');

        if (eq != std::string::npos)

        {

            std::string name = trim(pair.substr(0, eq));

            std::string value = pair.substr(eq + 1);

            if (name == key)

                return value;

        }

        if (end == std::string::npos)

            break;

        pos = end + 1;

    }

    return "";

}


/**
 * @brief 构建HTML页面外壳
 * @param title 页面标题
 * @param body 页面主体内容HTML
 * @return 完整的HTML页面字符串
 * 
 * 功能:
 *   将动态生成的body内容包装在统一的页面模板中,
 *   包括导航栏、CSS样式、JavaScript脚本等。
 * 
 * 用途:
 *   用于动态页面(欢迎页、上传结果页、错误页等)的生成,
 *   保持网站界面风格统一。
 */
std::string http_conn::build_page_shell(const std::string &title, const std::string &body) const

{

    std::ostringstream oss;

    oss << R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<link rel="icon" href="/assets/media/favicon.ico">
<link rel="stylesheet" href="/assets/css/site.css">
<title>WebServer | )HTML"
        << title
        << R"HTML(</title>
</head>
<body>

<div class="page">
<div class="nav">
<div class="brand">WebServer &#x5b9e;&#x9a8c;&#x7ad9;</div>
<div class="nav-links">
<a href="/">&#x9996;&#x9875;</a>
<a href="/uploads/list">&#x6211;&#x7684;&#x4e0a;&#x4f20;</a>
<a href="/pages/status.html">&#x76d1;&#x63a7;</a>
 </div>
 <div class="nav-auth">
 <a class="btn ghost" href="/pages/log.html">&#x767b;&#x5f55;</a>
 <a class="btn primary" href="/pages/register.html">&#x6ce8;&#x518c;</a>
 </div>
 </div>)HTML";
    oss << body;
    oss << R"HTML(</div>
<script src="/assets/js/nav-auth.js"></script>
</body>
</html>)HTML";
    return oss.str();
}


/**
 * @brief 处理服务器状态JSON接口
 * @return HTTP_CODE 返回DYNAMIC_REQUEST
 * 
 * 返回的JSON格式:
 *   {
 *     "uptime_seconds": 12345,        // 服务器运行时间(秒)
 *     "online_users": 10,             // 当前在线用户数(唯一IP)
 *     "online_connections": 15,       // 当前活动连接数
 *     "total_unique_visitors": 100,   // 历史唯一访客数
 *     "total_requests": 5000,         // 总请求数
 *     "avg_qps": 0.41,                // 平均每秒请求数
 *     "server_time": "2024-01-01 12:00:00"
 *   }
 * 
 * 用途: 供前端监控页面实时获取服务器状态
 */
http_conn::HTTP_CODE http_conn::handle_status_json()
{
    time_t now = time(nullptr);
    if (m_start_time == 0)
        m_start_time = now;

    // 获取统计数据
    long long total = m_total_requests.load();  // 原子读取总请求数
    long uptime = static_cast<long>(now - m_start_time);  // 运行时间
    double qps = uptime > 0 ? static_cast<double>(total) / uptime : static_cast<double>(total);

    // 格式化当前时间
    std::tm tm_snapshot{};
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &now);
#else
    localtime_r(&now, &tm_snapshot);
#endif
    char time_buf[32] = "-";
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_snapshot);

    // 统计在线用户数（唯一IP数量，已考虑Cloudflare穿透）
    size_t online_users = 0;
    size_t total_unique_ips = 0;
    m_ip_lock.lock();
    online_users = m_ip_counts.size();      // 当前在线的唯一IP数
    total_unique_ips = m_unique_ips.size(); // 历史所有唯一IP数
    m_ip_lock.unlock();

    // 构建JSON响应
    std::ostringstream oss;
    oss << "{" 
        << "\"uptime_seconds\":" << uptime << ","
        << "\"online_users\":" << online_users << ","
        << "\"online_connections\":" << m_user_count << ","
        << "\"total_unique_visitors\":" << total_unique_ips << ","
        << "\"total_requests\":" << total << ","
        << "\"avg_qps\":" << std::fixed << std::setprecision(2) << qps << ","
        << "\"server_time\":\"" << time_buf << "\""
        << "}";

    m_dynamic_content = oss.str();
    m_dynamic_content_type = "application/json; charset=utf-8";
    m_response_status = 200;
    // 禁用缓存，确保每次获取最新数据
    m_extra_headers += "Cache-Control: no-store, no-cache, must-revalidate\r\n";
    m_extra_headers += "Pragma: no-cache\r\n";
    return DYNAMIC_REQUEST;
}




http_conn::HTTP_CODE http_conn::handle_welcome_page()

{

    std::ostringstream body;

    body << R"HTML(<section class="hero">

<div>

<h1>&#x6b22;&#x8fce;&#x56de;&#x6765;&#xff0c;)HTML"

         << html_escape(m_username)

         << R"HTML(</h1>

<p>&#x8fd9;&#x91cc;&#x662f;&#x4f60;&#x7684;&#x4e2a;&#x4eba;&#x6f14;&#x793a;&#x7a7a;&#x95f4;&#xff0c;&#x53ef;&#x8bbf;&#x95ee;&#x56fe;&#x96c6;&#x3001;&#x89c6;&#x9891;&#x3001;&#x4e0a;&#x4f20;&#x4e2d;&#x5fc3;&#x4e0e;&#x5b9e;&#x65f6;&#x76d1;&#x63a7;&#x3002;</p>

<div class="actions">

<a class="btn primary" href="/uploads/list">&#x6211;&#x7684;&#x4e0a;&#x4f20;</a>

<a class="btn ghost" href="/pages/upload.html">&#x4e0a;&#x4f20;&#x6587;&#x4ef6;</a>

</div>
</div>

<div class="panel">

<h3>&#x5f53;&#x524d;&#x80fd;&#x529b;</h3>

<p style="margin-top: 12px; color: var(--muted);">&#x652f;&#x6301;&#x9759;&#x6001;&#x8d44;&#x6e90;&#x8bbf;&#x95ee;&#x3001;&#x7528;&#x6237;&#x9694;&#x79bb;&#x4e0a;&#x4f20;&#x3001;&#x5a92;&#x4f53;&#x5c55;&#x793a;&#x3001;JSON &#x76d1;&#x63a7;&#x4e0e; PHP &#x52a8;&#x6001;&#x89e3;&#x6790;&#x3002;</p>

</div>

</section>)HTML";



    m_dynamic_content = build_page_shell("&#x6b22;&#x8fce;", body.str());

    m_dynamic_content_type = "text/html; charset=utf-8";

    m_response_status = 200;

    return DYNAMIC_REQUEST;

}



/**
 * @brief 处理文件上传请求
 * @return HTTP_CODE 处理结果
 * 
 * multipart/form-data格式说明:
 *   --boundary\r\n
 *   Content-Disposition: form-data; name="file"; filename="xxx.jpg"\r\n
 *   Content-Type: image/jpeg\r\n
 *   \r\n
 *   <文件二进制内容>
 *   \r\n--boundary--\r\n
 * 
 * 处理流程:
 *   1. 验证请求方法和登录状态
 *   2. 解析boundary分隔符
 *   3. 解析Content-Disposition获取文件名
 *   4. 提取文件二进制内容
 *   5. 生成存储文件名（用户名_时间戳_原始名）
 *   6. 写入文件到uploads目录
 *   7. 更新用户元数据文件
 */
http_conn::HTTP_CODE http_conn::handle_upload_request()

{
    // 失败响应的lambda函数
    auto fail = [&](const std::string &message) {

        std::ostringstream body;

        body << R"HTML(<section class="panel" style="max-width: 620px; margin: 0 auto;">

<h2 style="font-size: 24px;">&#x4e0a;&#x4f20;&#x5931;&#x8d25;</h2>

<p style="margin-top: 8px; color: var(--muted);">)HTML"

             << message

             << R"HTML(</p>

<div class="actions" style="margin-top: 16px;">

<a class="btn primary" href="/pages/upload.html">&#x8fd4;&#x56de;&#x4e0a;&#x4f20;</a>


</div>

</section>)HTML";

        m_response_status = 400;

        m_dynamic_content_type = "text/html; charset=utf-8";

        m_dynamic_content = build_page_shell("&#x4e0a;&#x4f20;&#x5931;&#x8d25;", body.str());

        return DYNAMIC_REQUEST;

    };
    // 验证请求方法
    if (m_method != POST)

        return fail("&#x5f53;&#x524d;&#x8bf7;&#x6c42;&#x65b9;&#x6cd5;&#x4e0d;&#x652f;&#x6301;&#x4e0a;&#x4f20;&#x3002;");
    // 验证登录状态
    if (m_username.empty())

        return fail("&#x672a;&#x68c0;&#x6d4b;&#x5230;&#x767b;&#x5f55;&#x7528;&#x6237;&#x3002;");
    // 验证请求体
    if (!m_string || m_content_length <= 0)
        return fail("&#x672a;&#x68c0;&#x6d4b;&#x5230;&#x6709;&#x6548;&#x7684;&#x4e0a;&#x4f20;&#x5185;&#x5bb9;&#x3002;");

    // 请求体指针和长度
    const char *body_ptr = m_string;
    size_t body_len = static_cast<size_t>(m_content_length);
    const char *body_end = body_ptr + body_len;

    // 在二进制数据中查找子序列的辅助函数
    auto find_seq = [](const char *hay, size_t hay_len, const char *needle, size_t needle_len) -> const char * {
        if (!hay || !needle || needle_len == 0 || hay_len < needle_len)
            return nullptr;
        const char *end = hay + hay_len;
        const char *pos = std::search(hay, end, needle, needle + needle_len);
        if (pos == end)
            return nullptr;
        return pos;
    };

    auto find_line_end = [&](const char *start_ptr, size_t len, size_t &line_len) -> const char * {
        const char *crlf = find_seq(start_ptr, len, "\r\n", 2);
        if (crlf)
        {
            line_len = 2;
            return crlf;
        }
        const char *lf = find_seq(start_ptr, len, "\n", 1);
        if (lf)
        {
            line_len = 1;
            return lf;
        }
        return nullptr;
    };

    auto find_header_end = [&](const char *start_ptr, size_t len, size_t &sep_len) -> const char * {
        const char *crlf = find_seq(start_ptr, len, "\r\n\r\n", 4);
        if (crlf)
        {
            sep_len = 4;
            return crlf;
        }
        const char *lf = find_seq(start_ptr, len, "\n\n", 2);
        if (lf)
        {
            sep_len = 2;
            return lf;
        }
        return nullptr;
    };

    auto find_boundary_line = [&](const std::string &delim) -> const char * {
        if (delim.empty())
            return nullptr;
        if (body_len >= delim.size() && std::equal(body_ptr, body_ptr + delim.size(), delim.begin()))
            return body_ptr;
        std::string marker = "\r\n" + delim;
        const char *pos = find_seq(body_ptr, body_len, marker.data(), marker.size());
        if (pos)
            return pos + 2;
        marker = "\n" + delim;
        pos = find_seq(body_ptr, body_len, marker.data(), marker.size());
        if (pos)
            return pos + 1;
        return nullptr;
    };

    std::string boundary = m_boundary;
    if (!boundary.empty() && boundary.rfind("--", 0) != 0)
        boundary = "--" + boundary;

    size_t boundary_break_len = 0;
    bool boundary_break_known = false;

    const char *boundary_ptr = find_boundary_line(boundary);
    if (!boundary_ptr)
    {
        size_t line_len = 0;
        const char *line_end_ptr = find_line_end(body_ptr, body_len, line_len);
        if (!line_end_ptr)
            return fail("&#x4e0a;&#x4f20;&#x6570;&#x636e;&#x683c;&#x5f0f;&#x4e0d;&#x5b8c;&#x6574;&#x3002;");
        boundary.assign(body_ptr, static_cast<size_t>(line_end_ptr - body_ptr));
        if (boundary.empty())
            return fail("&#x7f3a;&#x5c11;&#x5206;&#x9694;&#x7b26;&#x3002;");
        boundary_ptr = body_ptr;
        boundary_break_len = line_len;
        boundary_break_known = true;
    }

    const char *after_boundary = boundary_ptr + boundary.size();
    if (boundary_break_known)
    {
        if (after_boundary + boundary_break_len > body_end)
            return fail("&#x4e0a;&#x4f20;&#x6570;&#x636e;&#x683c;&#x5f0f;&#x4e0d;&#x5b8c;&#x6574;&#x3002;");
    }
    else if (after_boundary + 1 <= body_end && after_boundary[0] == '\r' && after_boundary[1] == '\n')
    {
        boundary_break_len = 2;
    }
    else if (after_boundary < body_end && after_boundary[0] == '\n')
    {
        boundary_break_len = 1;
    }
    else
    {
        return fail("&#x4e0a;&#x4f20;&#x6570;&#x636e;&#x683c;&#x5f0f;&#x4e0d;&#x5b8c;&#x6574;&#x3002;");
    }
    const char *headers_start_ptr = after_boundary + boundary_break_len;
    if (headers_start_ptr > body_end)
        return fail("&#x4e0a;&#x4f20;&#x5934;&#x90e8;&#x89e3;&#x6790;&#x5931;&#x8d25;&#x3002;");
    size_t headers_remaining = static_cast<size_t>(body_end - headers_start_ptr);
    size_t header_sep_len = 0;
    const char *headers_end_ptr = find_header_end(headers_start_ptr, headers_remaining, header_sep_len);

    if (!headers_end_ptr)

        return fail("&#x4e0a;&#x4f20;&#x5934;&#x90e8;&#x89e3;&#x6790;&#x5931;&#x8d25;&#x3002;");


    const char *headers_ptr = headers_start_ptr;
    size_t headers_len = static_cast<size_t>(headers_end_ptr - headers_ptr);
    std::string headers(headers_ptr, headers_len);

    size_t filename_pos = headers.find("filename=\"");

    if (filename_pos == std::string::npos)

        return fail("&#x6ca1;&#x6709;&#x627e;&#x5230;&#x4e0a;&#x4f20;&#x6587;&#x4ef6;&#x540d;&#x3002;");

    filename_pos += 10;

    size_t filename_end = headers.find('"', filename_pos);

    if (filename_end == std::string::npos)

        return fail("&#x4e0a;&#x4f20;&#x6587;&#x4ef6;&#x540d;&#x89e3;&#x6790;&#x5931;&#x8d25;&#x3002;");


    std::string original_name = sanitize_filename(headers.substr(filename_pos, filename_end - filename_pos));

    if (original_name.empty())

        return fail("&#x4e0a;&#x4f20;&#x6587;&#x4ef6;&#x540d;&#x4e3a;&#x7a7a;&#x3002;");


    const char *data_start_ptr = headers_end_ptr + header_sep_len;
    if (data_start_ptr > body_end)
        return fail("&#x4e0a;&#x4f20;&#x5185;&#x5bb9;&#x622a;&#x65ad;&#x3002;");
    size_t data_remaining = static_cast<size_t>(body_end - data_start_ptr);
    
    LOG_INFO("Upload parse: boundary='%s' (len=%zu), data_remaining=%zu", 
             boundary.c_str(), boundary.size(), data_remaining);
    
    // 查找结束boundary - 注意结束boundary可能是 boundary-- 或 boundary
    // 先尝试找 boundary-- 形式（最后一个part的结束标记）
    std::string end_boundary = boundary + "--";
    std::string boundary_marker = "\r\n" + end_boundary;
    
    const char *data_end_ptr = find_seq(data_start_ptr, data_remaining, boundary_marker.data(), boundary_marker.size());
    if (!data_end_ptr)
    {
        std::string boundary_marker_lf = "\n" + end_boundary;
        data_end_ptr = find_seq(data_start_ptr, data_remaining, boundary_marker_lf.data(), boundary_marker_lf.size());
    }
    // 如果还没找到，尝试不带 -- 的普通 boundary（多part场景下）
    if (!data_end_ptr)
    {
        boundary_marker = "\r\n" + boundary;
        data_end_ptr = find_seq(data_start_ptr, data_remaining, boundary_marker.data(), boundary_marker.size());
    }
    if (!data_end_ptr)
    {
        std::string boundary_marker_lf = "\n" + boundary;
        data_end_ptr = find_seq(data_start_ptr, data_remaining, boundary_marker_lf.data(), boundary_marker_lf.size());
    }

    if (!data_end_ptr || data_end_ptr <= data_start_ptr)

        return fail("&#x4e0a;&#x4f20;&#x5185;&#x5bb9;&#x622a;&#x65ad;&#x3002;");


    size_t file_size = static_cast<size_t>(data_end_ptr - data_start_ptr);

    if (file_size == 0)

        return fail("&#x6587;&#x4ef6;&#x5185;&#x5bb9;&#x4e3a;&#x7a7a;&#x3002;");


    time_t now = time(nullptr);

    std::tm tm_snapshot{};

#if defined(_WIN32)

    localtime_s(&tm_snapshot, &now);

#else

    localtime_r(&now, &tm_snapshot);

#endif
    char ts_buf[32];
    std::strftime(ts_buf, sizeof(ts_buf), "%Y%m%d%H%M%S", &tm_snapshot);


    std::string stored_name = m_username + "_" + ts_buf + "_" + original_name;
    std::string upload_dir = std::string(doc_root) + "/uploads";
    std::string meta_dir = upload_dir + "/.meta";
    if (mkdir(upload_dir.c_str(), 0755) == -1 && errno != EEXIST)
        return fail("&#x65e0;&#x6cd5;&#x521b;&#x5efa;&#x4e0a;&#x4f20;&#x76ee;&#x5f55;&#x3002;");
    if (mkdir(meta_dir.c_str(), 0755) == -1 && errno != EEXIST)
        return fail("&#x65e0;&#x6cd5;&#x521b;&#x5efa;&#x5143;&#x6570;&#x636e;&#x76ee;&#x5f55;&#x3002;");


    std::string file_path = upload_dir + "/" + stored_name;
    int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0)
        return fail("&#x65e0;&#x6cd5;&#x5199;&#x5165;&#x4e0a;&#x4f20;&#x6587;&#x4ef6;&#x3002;");

    const char *file_data = data_start_ptr;
    size_t written_total = 0;

    while (written_total < file_size)
    {
        ssize_t written = ::write(fd, file_data + written_total, file_size - written_total);
        if (written <= 0)
        {
            close(fd);
            unlink(file_path.c_str());  // 删除不完整的文件
            return fail("&#x5199;&#x5165;&#x6587;&#x4ef6;&#x5931;&#x8d25;&#x3002;");
        }
        written_total += static_cast<size_t>(written);
    }

    close(fd);



    std::ofstream meta(meta_dir + "/" + m_username + ".list", std::ios::app);

    if (meta)

        meta << stored_name << "|" << original_name << "|" << file_size << "|" << now << "\n";



    std::string file_url = "/uploads/" + stored_name;

    std::ostringstream body_html;

    body_html << R"HTML(<section class="panel" style="max-width: 820px; margin: 0 auto;">

<h2 style="font-size: 26px;">&#x4e0a;&#x4f20;&#x6210;&#x529f;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x6587;&#x4ef6;&#x5df2;&#x4fdd;&#x5b58;&#xff0c;&#x4ec5;&#x672c;&#x4eba;&#x53ef;&#x89c1;&#x3002;</p>

<div class="grid" style="margin-top: 18px;">

<div class="card"><h3>&#x6587;&#x4ef6;&#x540d;</h3><p>)HTML"

              << html_escape(original_name)

              << R"HTML(</p></div>

<div class="card"><h3>&#x8bbf;&#x95ee;&#x5730;&#x5740;</h3><p><a href=")HTML" << file_url << R"HTML(">)HTML" << file_url << R"HTML(</a></p></div>

<div class="card"><h3>&#x6587;&#x4ef6;&#x5927;&#x5c0f;(&#x5b57;&#x8282;)</h3><p>)HTML"

              << file_size

              << R"HTML(</p></div>

</div>

<div class="actions" style="margin-top: 20px;">

<a class="btn primary" href=")HTML" << file_url << R"HTML(">&#x7acb;&#x5373;&#x67e5;&#x770b;</a>

<a class="btn ghost" href="/pages/upload.html">&#x7ee7;&#x7eed;&#x4e0a;&#x4f20;</a>


</div>

</section>)HTML";



    m_dynamic_content = build_page_shell("&#x4e0a;&#x4f20;&#x6210;&#x529f;", body_html.str());

    m_dynamic_content_type = "text/html; charset=utf-8";

    m_response_status = 200;

    return DYNAMIC_REQUEST;

}



http_conn::HTTP_CODE http_conn::handle_upload_list()

{

    std::vector<UploadItem> items;

    load_user_uploads(doc_root, m_username, items);



    std::ostringstream body;

    body << R"HTML(<section class="panel" style="max-width: 980px; margin: 0 auto;">

<h2 style="font-size: 26px;">&#x6211;&#x7684;&#x4e0a;&#x4f20;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x4ee5;&#x4e0b;&#x5185;&#x5bb9;&#x4ec5;&#x5bf9;&#x5f53;&#x524d;&#x8d26;&#x53f7;&#x53ef;&#x89c1;&#x3002;</p>)HTML";



    if (items.empty())

    {

        body << R"HTML(<p style="margin-top: 16px;">&#x6682;&#x65e0;&#x4e0a;&#x4f20;&#x5185;&#x5bb9;&#xff0c;&#x5148;&#x53bb;&#x4e0a;&#x4f20;&#x4e00;&#x4efd;&#x5427;&#x3002;</p>)HTML";

    }

    else

    {

        body << R"HTML(<div class="grid" style="margin-top: 18px;">)HTML";

        for (const auto &item : items)

        {

            std::string url = "/uploads/" + item.stored_name;
            std::string name_lower = to_lower_copy(item.stored_name);
            size_t pos = name_lower.find_last_of('.');
            std::string ext = pos == std::string::npos ? "" : name_lower.substr(pos);

            body << R"HTML(<div class="card">)HTML";
            if (is_image_ext(ext))
            {
                body << R"HTML(<img src=")HTML" << url
                     << R"HTML(" alt=")HTML" << html_escape(item.original_name)
                     << R"HTML(" style="width:100%; border-radius: 18px; margin-bottom: 12px;">)HTML";
            }
            else if (is_video_ext(ext))
            {
                body << R"HTML(<video src=")HTML" << url
                     << R"HTML(" controls preload="metadata" style="width:100%; border-radius: 18px; margin-bottom: 12px;"></video>)HTML";
            }

            body << R"HTML(<h3>)HTML" << html_escape(item.original_name)

                 << R"HTML(</h3><p style="margin-top: 8px;">&#x4e0a;&#x4f20;&#x65f6;&#x95f4;&#xff1a;)HTML"

                 << format_time(static_cast<time_t>(item.timestamp))

                 << R"HTML(</p><p>&#x6587;&#x4ef6;&#x5927;&#x5c0f;&#xff1a;)HTML" << item.size << R"HTML( &#x5b57;&#x8282;</p>

<a href=")HTML" << url << R"HTML(" class="btn ghost" style="margin-top: 12px; display: inline-flex;">&#x67e5;&#x770b;</a>

<form action="/uploads/delete" method="post" style="margin-top: 10px;">
<input type="hidden" name="file" value=")HTML" << html_escape(item.stored_name) << R"HTML(">
<button class="btn ghost" type="submit">&#x5220;&#x9664;</button>
</form></div>)HTML";

        }
        body << R"HTML(</div>)HTML";

    }



    body << R"HTML(<div class="actions" style="margin-top: 20px;">

<a class="btn primary" href="/pages/upload.html">&#x7ee7;&#x7eed;&#x4e0a;&#x4f20;</a>


</div>

</section>)HTML";



    m_dynamic_content = build_page_shell("&#x6211;&#x7684;&#x4e0a;&#x4f20;", body.str());

    m_dynamic_content_type = "text/html; charset=utf-8";

    m_response_status = 200;

    return DYNAMIC_REQUEST;

}



http_conn::HTTP_CODE http_conn::handle_upload_delete()
{

    auto fail = [&](const std::string &message, int status) {

        std::ostringstream body;

        body << R"HTML(<section class="panel" style="max-width: 620px; margin: 0 auto;">

<h2 style="font-size: 24px;">&#x5220;&#x9664;&#x5931;&#x8d25;</h2>

<p style="margin-top: 8px; color: var(--muted);">)HTML"

             << message

             << R"HTML(</p>

<div class="actions" style="margin-top: 16px;">

<a class="btn primary" href="/uploads/list">&#x8fd4;&#x56de;&#x6211;&#x7684;&#x4e0a;&#x4f20;</a>

<a class="btn ghost" href="/pages/upload.html">&#x4e0a;&#x4f20;&#x6587;&#x4ef6;</a>

</div>

</section>)HTML";

        m_response_status = status;

        m_dynamic_content_type = "text/html; charset=utf-8";

        m_dynamic_content = build_page_shell("&#x5220;&#x9664;&#x5931;&#x8d25;", body.str());

        return DYNAMIC_REQUEST;

    };

    if (m_method != POST)

        return fail("&#x5f53;&#x524d;&#x8bf7;&#x6c42;&#x65b9;&#x6cd5;&#x4e0d;&#x652f;&#x6301;&#x5220;&#x9664;&#x3002;", 400);

    if (m_username.empty())

        return fail("&#x672a;&#x68c0;&#x6d4b;&#x5230;&#x767b;&#x5f55;&#x7528;&#x6237;&#x3002;", 400);

    if (!m_string || m_content_length <= 0)

        return fail("&#x672a;&#x68c0;&#x6d4b;&#x5230;&#x6709;&#x6548;&#x7684;&#x5220;&#x9664;&#x8bf7;&#x6c42;&#x3002;", 400);

    std::string payload(m_string, static_cast<size_t>(m_content_length));
    std::string stored = get_form_value(payload, "file");
    if (stored.empty())
        stored = get_form_value(payload, "stored");
    if (stored.empty())
        return fail("&#x672a;&#x627e;&#x5230;&#x8981;&#x5220;&#x9664;&#x7684;&#x6587;&#x4ef6;&#x3002;", 400);
    if (stored.find("..") != std::string::npos || stored.find('/') != std::string::npos || stored.find('\\') != std::string::npos)
        return fail("&#x6587;&#x4ef6;&#x540d;&#x4e0d;&#x5408;&#x6cd5;&#x3002;", 400);
    if (!user_owns_upload(m_username, stored))
        return fail("&#x6ca1;&#x6709;&#x6743;&#x9650;&#x5220;&#x9664;&#x8be5;&#x6587;&#x4ef6;&#x3002;", 404);

    std::string file_path = std::string(doc_root) + "/uploads/" + stored;
    if (::remove(file_path.c_str()) != 0 && errno != ENOENT)
        return fail("&#x5220;&#x9664;&#x6587;&#x4ef6;&#x5931;&#x8d25;&#x3002;", 500);

    std::string meta_path = std::string(doc_root) + "/uploads/.meta/" + m_username + ".list";
    std::ifstream in(meta_path);
    if (!in)
        return fail("&#x672a;&#x627e;&#x5230;&#x4e0a;&#x4f20;&#x8bb0;&#x5f55;&#x3002;", 404);
    std::string tmp_path = meta_path + ".tmp";
    std::ofstream out(tmp_path, std::ios::trunc);
    if (!out)
        return fail("&#x65e0;&#x6cd5;&#x66f4;&#x65b0;&#x4e0a;&#x4f20;&#x8bb0;&#x5f55;&#x3002;", 500);
    std::string line;
    bool removed = false;
    while (std::getline(in, line))
    {
        if (line.empty())
            continue;
        std::stringstream ss(line);
        std::string stored_name;
        if (!std::getline(ss, stored_name, '|'))
            continue;
        if (stored_name == stored)
        {
            removed = true;
            continue;
        }
        out << line << "\n";
    }
    in.close();
    out.close();
    if (!removed)
    {
        ::remove(tmp_path.c_str());
        return fail("&#x672a;&#x627e;&#x5230;&#x8981;&#x5220;&#x9664;&#x7684;&#x8bb0;&#x5f55;&#x3002;", 404);
    }
    if (::rename(tmp_path.c_str(), meta_path.c_str()) != 0)
        return fail("&#x66f4;&#x65b0;&#x8bb0;&#x5f55;&#x5931;&#x8d25;&#x3002;", 500);

    std::ostringstream body;
    body << R"HTML(<section class="panel" style="max-width: 620px; margin: 0 auto;">

<h2 style="font-size: 24px;">&#x5220;&#x9664;&#x6210;&#x529f;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x6587;&#x4ef6;&#x5df2;&#x5220;&#x9664;&#x3002;</p>

<div class="actions" style="margin-top: 16px;">

<a class="btn primary" href="/uploads/list">&#x8fd4;&#x56de;&#x6211;&#x7684;&#x4e0a;&#x4f20;</a>

<a class="btn ghost" href="/pages/upload.html">&#x4e0a;&#x4f20;&#x6587;&#x4ef6;</a>

</div>

</section>)HTML";

    m_response_status = 200;

    m_dynamic_content_type = "text/html; charset=utf-8";

    m_dynamic_content = build_page_shell("&#x5220;&#x9664;&#x6210;&#x529f;", body.str());

    return DYNAMIC_REQUEST;

}


bool http_conn::user_owns_upload(const std::string &owner, const std::string &stored_name) const

{

    std::vector<UploadItem> items;

    if (!load_user_uploads(doc_root, owner, items))

        return false;

    for (const auto &item : items)

    {

        if (item.stored_name == stored_name)

            return true;

    }

    return false;

}



/**
 * @brief 处理HTTP请求的主逻辑
 * @return HTTP_CODE 处理结果
 * 
 * 功能说明:
 *   根据URL和请求方法执行相应处理:
 * 
 * 1. URL重定向:
 *    - /register.html -> /pages/register.html
 *    - /log.html -> /pages/log.html
 *    - /picture.html -> /uploads/list
 *    - 等等...
 * 
 * 2. 特殊URL处理:
 *    - /2: 登录请求 (CGI)
 *    - /3: 注册请求 (CGI)
 *    - /logout: 退出登录
 *    - /status.json: 服务器状态JSON
 *    - /upload: 文件上传
 *    - /uploads/delete: 删除文件
 *    - /uploads/list: 文件列表
 * 
 * 3. 权限检查:
 *    - 通过Cookie验证登录状态
 *    - 未登录用户访问受保护资源时重定向到登录页
 * 
 * 4. 静态文件处理:
 *    - 使用stat()获取文件信息
 *    - 使用mmap()映射文件到内存
 *    - 根据扩展名设置Content-Type
 * 
 * 5. PHP动态处理:
 *    - 调用系统PHP解释器执行.php文件
 */
http_conn::HTTP_CODE http_conn::do_request()

{
    // URL解码和验证
    std::string url = m_url ? m_url : "/";
    if (url.empty())
        url = "/";
    // url_decode(): 将%XX转换为实际字符
    url = url_decode(url);
    if (url.empty() || url[0] != '/')
        return BAD_REQUEST;
    // 安全检查: 禁止目录穿越攻击
    if (url.find("..") != std::string::npos)
        return BAD_REQUEST;


    if (url == "/register.html")

        url = "/pages/register.html";

    else if (url == "/log.html")

        url = "/pages/log.html";

    else if (url == "/welcome.html")

        url = "/pages/welcome.html";

    else if (url == "/picture.html" || url == "/video.html" ||
             url == "/pages/picture.html" || url == "/pages/video.html")

        url = "/uploads/list";

    else if (url == "/upload.html")

        url = "/pages/upload.html";

    else if (url == "/status.html")

        url = "/pages/status.html";
    if (url.size() == 2 && url[0] == '/')

    {

        switch (url[1])

        {

        case '0':

            url = "/pages/register.html";

            break;

        case '1':

            url = "/pages/log.html";

            break;

        case '5':

            url = "/uploads/list";

            break;

        case '6':

            url = "/uploads/list";

            break;

        case '8':

            url = "/index.html";

            break;

        case '9':

            url = "/404.html";

            break;

        default:

            break;

        }

    }



    std::string cookie_user = get_cookie_value("ws_user");

    bool logged_in = !cookie_user.empty() && users.find(cookie_user) != users.end();

    if (logged_in)

        m_username = cookie_user;

    else if (!cookie_user.empty())

        m_extra_headers += "Set-Cookie: ws_user=; Path=/; Max-Age=0\r\n";



    auto redirect_login = [&]() -> HTTP_CODE {

        m_response_status = 302;

        m_extra_headers += "Location: /pages/log.html\r\n";

        m_dynamic_content_type = "text/html; charset=utf-8";

        m_dynamic_content = build_page_shell(

            "&#x9700;&#x8981;&#x767b;&#x5f55;",

            R"HTML(<section class="panel" style="max-width: 620px; margin: 0 auto;">

<h2 style="font-size: 24px;">&#x8bf7;&#x5148;&#x767b;&#x5f55;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x8be5;&#x529f;&#x80fd;&#x4ec5;&#x5bf9;&#x5df2;&#x767b;&#x5f55;&#x7528;&#x6237;&#x5f00;&#x653e;&#x3002;</p>

<div class="actions" style="margin-top: 16px;">

<a class="btn primary" href="/pages/log.html">&#x524d;&#x5f80;&#x767b;&#x5f55;</a>

<a class="btn ghost" href="/pages/register.html">&#x6ce8;&#x518c;&#x8d26;&#x53f7;</a>

</div>

</section>)HTML");

        return DYNAMIC_REQUEST;

    };



    auto render_not_found = [&]() -> HTTP_CODE {

        std::string not_found_path = std::string(doc_root) + "/404.html";

        std::ifstream in(not_found_path);

        if (in)

        {

            std::ostringstream ss;

            ss << in.rdbuf();

            m_dynamic_content = ss.str();

            m_dynamic_content_type = "text/html; charset=utf-8";

            m_response_status = 404;

            return DYNAMIC_REQUEST;

        }

        return NO_RESOURCE;

    };



    if (cgi == 1 && url.size() > 1 && (url[1] == '2' || url[1] == '3'))

    {

        if (!m_string)

            return BAD_REQUEST;

        std::string payload = m_string;

        size_t user_pos = payload.find("user=");

        size_t pass_pos = payload.find("password=");

        if (user_pos == std::string::npos || pass_pos == std::string::npos)

            return BAD_REQUEST;

        size_t user_end = payload.find('&', user_pos);

        if (user_end == std::string::npos)

            user_end = pass_pos - 1;

        std::string name = payload.substr(user_pos + 5, user_end - (user_pos + 5));

        std::string password = payload.substr(pass_pos + 9);



        if (url[1] == '3')

        {

            std::string sql = "INSERT INTO user(username, passwd) VALUES('" + name + "','" + password + "')";

            if (users.find(name) == users.end())

            {

                m_lock.lock();

                int res = mysql_query(mysql, sql.c_str());

                users.insert(pair<string, string>(name, password));

                m_lock.unlock();

                if (!res)

                    url = "/pages/log.html";

                else

                    url = "/pages/registerError.html";

            }

            else

            {

                url = "/pages/registerError.html";

            }

        }

        else if (url[1] == '2')

        {

            if (users.find(name) != users.end() && users[name] == password)

            {

                logged_in = true;

                m_username = name;

                m_extra_headers += "Set-Cookie: ws_user=";

                m_extra_headers += name;

                m_extra_headers += "; Path=/\r\n";
                url = "/pages/welcome.html";

            }

            else

            {

                url = "/pages/logError.html";

            }

        }

    }



    if (url == "/logout")

    {

        m_response_status = 302;

        m_extra_headers += "Set-Cookie: ws_user=; Path=/; Max-Age=0\r\n";

        m_extra_headers += "Location: /pages/log.html\r\n";

        m_dynamic_content_type = "text/html; charset=utf-8";

        m_dynamic_content = build_page_shell(

            "&#x9000;&#x51fa;&#x767b;&#x5f55;",

            R"HTML(<section class="panel" style="max-width: 620px; margin: 0 auto;">

<h2 style="font-size: 24px;">&#x5df2;&#x9000;&#x51fa;&#x767b;&#x5f55;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x4f60;&#x5df2;&#x5b89;&#x5168;&#x9000;&#x51fa;&#xff0c;&#x53ef;&#x4ee5;&#x91cd;&#x65b0;&#x767b;&#x5f55;&#x3002;</p>

<div class="actions" style="margin-top: 16px;">

<a class="btn primary" href="/pages/log.html">&#x524d;&#x5f80;&#x767b;&#x5f55;</a>


</div>

</section>)HTML");

        return DYNAMIC_REQUEST;

    }



    if (url == "/status.json")

    {

        if (!logged_in)

            return redirect_login();

        return handle_status_json();

    }



    if (url == "/upload")

    {

        if (!logged_in)

            return redirect_login();

        if (m_method == POST)

            return handle_upload_request();

        url = "/pages/upload.html";

    }



    if (url == "/uploads/delete")
    {

        if (!logged_in)

            return redirect_login();

        return handle_upload_delete();

    }
    if (url == "/uploads/list")

    {

        if (!logged_in)

            return redirect_login();

        return handle_upload_list();

    }



    if (url.rfind("/uploads/", 0) == 0)

    {

        if (!logged_in)

            return redirect_login();

        std::string stored = url.substr(std::string("/uploads/").size());

        if (stored.empty() || !user_owns_upload(m_username, stored))

            return render_not_found();

    }



    if (url == "/pages/status.html" || url == "/pages/upload.html" || url == "/pages/welcome.html")

    {

        if (!logged_in)

            return redirect_login();

    }

    if (url == "/pages/welcome.html")

        return handle_welcome_page();







    size_t ext_pos = url.find_last_of('.');

    std::string ext = ext_pos == std::string::npos ? "" : to_lower_copy(url.substr(ext_pos));

    if (ext == ".php")

    {

        std::string php_path = std::string(doc_root) + url;

        if (access(php_path.c_str(), F_OK) != 0)

            return render_not_found();

        if (!execute_php(php_path.c_str()))

            return INTERNAL_ERROR;

        m_dynamic_content_type = "text/html; charset=utf-8";

        m_response_status = 200;

        return PHP_REQUEST;

    }



    // 构建完整的文件路径
    snprintf(m_real_file, FILENAME_LEN, "%s%s", doc_root, url.c_str());
    
    // stat(): <sys/stat.h> 获取文件状态信息
    //   * 参数1: 文件路径
    //   * 参数2: stat结构体指针（输出参数）
    //   * 返回: 成功返回0，失败返回-1
    //   * stat结构体重要字段:
    //     - st_mode: 文件类型和权限
    //     - st_size: 文件大小（字节）
    //     - st_mtime: 最后修改时间
    if (stat(m_real_file, &m_file_stat) < 0)

        return render_not_found();
    // S_IROTH: 其他用户可读权限位
    if (!(m_file_stat.st_mode & S_IROTH))

        return FORBIDDEN_REQUEST;
    // S_ISDIR(): 判断是否为目录
    if (S_ISDIR(m_file_stat.st_mode))

        return BAD_REQUEST;



    if (ext == ".html" || ext == ".htm")

        m_content_type = "text/html; charset=utf-8";

    else if (ext == ".css")

        m_content_type = "text/css; charset=utf-8";

    else if (ext == ".js")

        m_content_type = "application/javascript; charset=utf-8";

    else if (ext == ".json")

        m_content_type = "application/json; charset=utf-8";

    else if (ext == ".png")

        m_content_type = "image/png";

    else if (ext == ".jpg" || ext == ".jpeg")

        m_content_type = "image/jpeg";

    else if (ext == ".gif")

        m_content_type = "image/gif";

    else if (ext == ".svg")

        m_content_type = "image/svg+xml";

    else if (ext == ".ico")

        m_content_type = "image/x-icon";

    else if (ext == ".mp4")

        m_content_type = "video/mp4";

    else if (ext == ".webm")

        m_content_type = "video/webm";

    else if (ext == ".ogg")

        m_content_type = "video/ogg";

    else if (ext == ".pdf")

        m_content_type = "application/pdf";

    else

        m_content_type = "application/octet-stream";


    // 打开文件并通过mmap映射到内存
    // open(): <fcntl.h> 打开文件
    //   * O_RDONLY: 只读模式
    //   * 返回文件描述符，失败返回-1
    int fd = open(m_real_file, O_RDONLY);
    
    // mmap(): <sys/mman.h> 将文件映射到内存
    //   * 参数1: 映射起始地址（0表示由系统选择）
    //   * 参数2: 映射长度（文件大小）
    //   * 参数3: 保护标志（PROT_READ只读）
    //   * 参数4: 映射类型（MAP_PRIVATE私有映射，写时复制）
    //   * 参数5: 文件描述符
    //   * 参数6: 文件偏移量（从头开始）
    //   * 返回: 映射区域起始地址，失败返回MAP_FAILED
    // 
    // 零拷贝优势: 文件内容直接从内核缓冲区发送，无需拷贝到用户空间
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    m_is_mmap = true;
    // 映射完成后可以关闭fd，映射仍然有效
    close(fd);

    return FILE_REQUEST;

}



/**
 * @brief 执行PHP脚本并获取输出
 * @param php_path PHP文件的完整路径
 * @return true执行成功
 * 
 * 实现原理:
 *   通过popen()调用系统PHP解释器执行脚本,
 *   捕获标准输出作为HTTP响应内容。
 * 
 * 库函数说明:
 *   - popen(): 创建管道执行shell命令
 *     * 参数1: 要执行的命令
 *     * 参数2: 打开模式 ("r"读取输出)
 *     * 返回: FILE*指针，失败返回NULL
 * 
 *   - fread(): 从流读取二进制数据
 *     * 参数: 缓冲区, 元素大小, 元素个数, 文件流
 *     * 返回: 实际读取的元素个数
 * 
 *   - pclose(): 关闭popen打开的管道
 *     * 会等待子进程结束
 *     * 返回子进程的退出状态
 * 
 *   - realloc(): 重新分配内存块
 *     * 用于动态扩展输出缓冲区
 * 
 * 注意: 需要系统安装PHP且在PATH中
 */
bool http_conn::execute_php(const char *php_path)
{
    if (php_path == nullptr || strlen(php_path) == 0)
    {
        LOG_ERROR("Invalid PHP path provided.");
        return false;
    }

    // 构建PHP执行命令，重定向stderr到stdout
    char php_command[512];
    snprintf(php_command, sizeof(php_command), "php %s 2>&1", php_path);

    // popen(): 创建管道执行命令
    FILE *fp = popen(php_command, "r");
    if (fp == nullptr)
    {
        LOG_ERROR("Failed to execute PHP: %s", php_command);
        // PHP未安装时的降级处理
        const char *fallback =
            "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\"><title>PHP Error</title></head>"
            "<body><h2>PHP &#x672a;&#x5c31;&#x7eea;</h2>"
            "<p>&#x65e0;&#x6cd5;&#x8c03;&#x7528; PHP &#x89e3;&#x91ca;&#x5668;&#xff0c;&#x8bf7;&#x786e;&#x8ba4;&#x5df2;&#x5b89;&#x88c5; PHP &#x5e76;&#x52a0;&#x5165; PATH&#x3002;</p>"
            "</body></html>";
        m_php_content_size = strlen(fallback);
        m_php_content = (char *)malloc(m_php_content_size + 1);
        if (!m_php_content)
            return false;
        memcpy(m_php_content, fallback, m_php_content_size);
        m_php_content[m_php_content_size] = '\0';
        m_file_address = m_php_content;
        m_file_stat.st_size = m_php_content_size;
        m_is_mmap = false;
        return true;
    }

    // 读取PHP输出
    char buffer[4096];
    size_t read_size = 0;
    size_t total_size = 0;
    size_t capacity = sizeof(buffer);

    // 初始分配内存
    m_php_content = (char *)malloc(capacity);
    if (m_php_content == nullptr)
    {
        LOG_ERROR("Memory allocation failed for PHP output.");
        pclose(fp);
        return false;
    }

    // 循环读取PHP输出
    while ((read_size = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
        // 需要扩展缓冲区
        if (total_size + read_size + 1 > capacity)
        {

            
            size_t new_capacity = std::max(capacity * 2, total_size + read_size + 1);
            char *new_buf = (char *)realloc(m_php_content, new_capacity);
            if (!new_buf)
            {
                LOG_ERROR("Memory reallocation failed for PHP output.");
                pclose(fp);
                free(m_php_content);
                m_php_content = NULL;
                return false;
            }
            m_php_content = new_buf;
            capacity = new_capacity;
        }
        memcpy(m_php_content + total_size, buffer, read_size);
        total_size += read_size;
    }
    // 关闭管道
    pclose(fp);

    m_php_content[total_size] = '\0';
    m_php_content_size = total_size;
    // PHP输出为空时的降级处理
    if (m_php_content_size == 0)
    {
        const char *fallback =
            "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"UTF-8\"><title>PHP Error</title></head>"
            "<body><h2>PHP &#x8f93;&#x51fa;&#x4e3a;&#x7a7a;</h2>"
            "<p>&#x8bf7;&#x786e;&#x8ba4; phpinfo.php &#x53ef;&#x88ab;&#x89e3;&#x6790;&#xff0c;&#x6216;&#x68c0;&#x67e5; PHP &#x662f;&#x5426;&#x5df2;&#x6b63;&#x786e;&#x5b89;&#x88c5;&#x3002;</p>"
            "</body></html>";
        free(m_php_content);
        m_php_content_size = strlen(fallback);
        m_php_content = (char *)malloc(m_php_content_size + 1);
        if (!m_php_content)
            return false;
        memcpy(m_php_content, fallback, m_php_content_size);
        m_php_content[m_php_content_size] = '\0';
    }

    m_file_address = m_php_content;
    m_file_stat.st_size = m_php_content_size;
    m_is_mmap = false;  // 标记为非mmap内存，需要free释放
    return true;
}

/**
 * @brief 释放内存映射的文件资源
 * 
 * 根据文件类型选择释放方式：
 * - mmap映射的静态文件：使用munmap释放
 * - PHP动态内容：使用free释放
 * 
 * 库函数说明：
 * - munmap(): <sys/mman.h> 解除内存映射
 *   * 参数1: 映射区域的起始地址（mmap返回值）
 *   * 参数2: 映射区域的大小
 *   * 成功返回0，失败返回-1
 *   * 必须与mmap成对使用，避免内存泄漏
 */
void http_conn::unmap()

{

    if (m_is_mmap && m_file_address)

    {
        // munmap(): 解除mmap建立的内存映射
        munmap(m_file_address, m_file_stat.st_size);

    }

    else if (m_file_address && m_file_address == m_php_content)

    {

        free(m_php_content);

        m_php_content = NULL;

        m_php_content_size = 0;

    }

    m_file_address = NULL;

    m_is_mmap = false;

}


/**
 * @brief 向客户端发送HTTP响应
 * 
 * @return true 发送成功或需要继续发送
 * @return false 发送失败，连接应关闭
 * 
 * 使用writev实现分散写入（scatter write）：
 * - 第一块：HTTP响应头（m_write_buf）
 * - 第二块：响应体/文件内容（m_file_address）
 * 
 * 库函数说明：
 * - writev(): <sys/uio.h> 分散写入（聚集写）
 *   * 参数1: 文件描述符
 *   * 参数2: iovec结构数组
 *   * 参数3: 数组元素个数
 *   * 返回: 实际写入的字节数，失败返回-1
 *   * 优点: 避免多次系统调用，减少数据拷贝
 *   * iovec结构: { void *iov_base; size_t iov_len; }
 * 
 * 非阻塞写入处理：
 * - errno=EAGAIN: 发送缓冲区满，需等待可写事件
 * - 部分发送: 更新指针和剩余长度，继续发送
 */
bool http_conn::write()

{

    int temp = 0;

    // 没有数据要发送，直接返回成功
    if (bytes_to_send == 0)

    {
        // 切换回监听读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

        init();

        return true;

    }

    // 循环发送数据
    while (1)

    {
        // writev(): 一次性发送多个缓冲区的数据
        // m_iv[0]: 响应头  m_iv[1]: 响应体
        temp = writev(m_sockfd, m_iv, m_iv_count);



        if (temp < 0)

        {
            // EAGAIN: 发送缓冲区满，等待下次可写
            if (errno == EAGAIN)

            {
                // 注册写事件，等待缓冲区可用
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);

                return true;

            }
            // 其他错误，清理资源返回失败
            unmap();

            return false;

        }

        // 更新已发送和待发送字节数
        bytes_have_send += temp;

        bytes_to_send -= temp;
        // 响应头已发送完毕
        if (static_cast<size_t>(bytes_have_send) >= m_iv[0].iov_len)
        {
            // 清空第一块，更新第二块指针
            m_iv[0].iov_len = 0;

            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);

            m_iv[1].iov_len = bytes_to_send;

        }

        else

        {
            // 响应头部分发送，更新第一块指针
            m_iv[0].iov_base = m_write_buf + bytes_have_send;

            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;

        }

        // 全部发送完毕
        if (bytes_to_send <= 0)

        {
            // 释放文件映射内存
            unmap();
            // 切换回监听读事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            // 长连接: 重置状态，等待下一个请求
            if (m_linger)

            {

                init();

                return true;

            }

            else

            {
                // 短连接: 返回false触发关闭
                return false;

            }

        }

    }

}



/**
 * @brief 向写缓冲区追加格式化数据
 * @param format printf风格的格式化字符串
 * @return true 写入成功
 * @return false 缓冲区已满
 * 
 * 库函数说明:
 *   - va_list: 可变参数列表类型
 *   - va_start(): 初始化可变参数列表
 *   - va_end(): 清理可变参数列表
 *   - vsnprintf(): 安全的格式化写入
 *     * 参数1: 目标缓冲区
 *     * 参数2: 缓冲区大小
 *     * 参数3: 格式化字符串
 *     * 参数4: 可变参数列表
 *     * 返回: 实际写入的字符数
 */
bool http_conn::add_response(const char *format, ...)

{
    // 检查缓冲区是否已满
    if (m_write_idx >= WRITE_BUFFER_SIZE)

        return false;
    // 初始化可变参数列表
    va_list arg_list;

    va_start(arg_list, format);
    // 安全地格式化写入缓冲区
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    // 检查是否截断
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))

    {

        va_end(arg_list);

        return false;

    }
    // 更新写入索引
    m_write_idx += len;

    va_end(arg_list);



    LOG_INFO("request:%s", m_write_buf);

    return true;

}



/**
 * @brief 添加HTTP状态行
 * @param status HTTP状态码 (200, 404, 500等)
 * @param title 状态描述 ("OK", "Not Found"等)
 * @return true写入成功
 * 
 * 状态行格式: "HTTP/1.1 200 OK\r\n"
 */
bool http_conn::add_status_line(int status, const char *title)

{

    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);

}


/**
 * @brief 添加HTTP响应头部
 * @param content_len 响应体长度
 * @return true写入成功
 * 
 * 添加的响应头包括:
 *   - Content-Length: 响应体大小
 *   - Content-Type: MIME类型
 *   - Connection: keep-alive/close
 *   - 额外自定义头部
 *   - 空行 (标记头部结束)
 */
bool http_conn::add_headers(int content_len)

{
    // 添加Content-Length
    bool ok = add_content_length(content_len);
    // 添加额外的自定义头部(如Set-Cookie, Location等)
    if (!m_extra_headers.empty())

        ok = ok && add_response("%s", m_extra_headers.c_str());
    // 添加Content-Type
    ok = ok && add_content_type();
    // 添加Connection
    ok = ok && add_linger();
    // 添加空行
    ok = ok && add_blank_line();

    return ok;

}


/**
 * @brief 添加Content-Length头部
 * @param content_len 响应体长度(字节)
 * 
 * 格式: "Content-Length:1234\r\n"
 */
bool http_conn::add_content_length(int content_len)

{

    return add_response("Content-Length:%d\r\n", content_len);

}


/**
 * @brief 添加Content-Type头部
 * @return true写入成功
 * 
 * 根据请求类型选择MIME类型:
 *   - 动态内容使用m_dynamic_content_type
 *   - 静态文件使用m_content_type (根据扩展名确定)
 *   - 默认使用text/html
 * 
 * 常见MIME类型:
 *   - text/html: HTML页面
 *   - application/json: JSON数据
 *   - image/png: PNG图片
 *   - video/mp4: MP4视频
 */
bool http_conn::add_content_type()

{

    const std::string &type = !m_dynamic_content_type.empty() ? m_dynamic_content_type : m_content_type;

    if (!type.empty())

        return add_response("Content-Type:%s\r\n", type.c_str());

    return add_response("Content-Type:%s\r\n", "text/html; charset=utf-8");

}


/**
 * @brief 添加Connection头部
 * @return true写入成功
 * 
 * 连接类型:
 *   - keep-alive: 长连接，一次TCP连接可处理多个请求
 *   - close: 短连接，每个请求后关闭连接
 */
bool http_conn::add_linger()

{

    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");

}


/**
 * @brief 添加空行（标记响应头结束）
 * @return true写入成功
 * 
 * HTTP协议规定响应头和响应体之间必须有一个空行
 */
bool http_conn::add_blank_line()

{

    return add_response("%s", "\r\n");

}


/**
 * @brief 添加响应体内容
 * @param content 响应体字符串
 * @return true写入成功
 * 
 * 注意: 此函数将内容直接写入写缓冲区
 *       大文件使用mmap映射而非此函数
 */
bool http_conn::add_content(const char *content)

{

    return add_response("%s", content);

}



/**
 * @brief 根据处理结果构建HTTP响应
 * @param ret HTTP处理结果码
 * @return true 响应构建成功
 * @return false 响应构建失败
 * 
 * HTTP响应格式:
 *   HTTP/1.1 200 OK\r\n             <- 状态行
 *   Content-Length: 1234\r\n        <- 响应头
 *   Content-Type: text/html\r\n
 *   Connection: keep-alive\r\n
 *   \r\n                              <- 空行
 *   <html>...</html>                <- 响应体
 * 
 * 响应类型处理:
 *   - INTERNAL_ERROR: 500内部错误
 *   - BAD_REQUEST: 400错误请求
 *   - NO_RESOURCE: 404资源不存在
 *   - FORBIDDEN_REQUEST: 403禁止访问
 *   - DYNAMIC_REQUEST: 动态内容响应
 *   - FILE_REQUEST: 文件响应(使用mmap)
 *   - PHP_REQUEST: PHP执行结果
 * 
 * iovec分散写入说明:
 *   m_iv[0]: 响应头 (m_write_buf)
 *   m_iv[1]: 响应体 (文件内容/动态内容)
 */
bool http_conn::process_write(HTTP_CODE ret)

{

    switch (ret)

    {

    case INTERNAL_ERROR:

    {

        add_status_line(500, error_500_title);

        add_headers(strlen(error_500_form));

        if (!add_content(error_500_form))

            return false;

        break;

    }

    case BAD_REQUEST:

    {

        add_status_line(400, error_400_title);

        add_headers(strlen(error_400_form));

        if (!add_content(error_400_form))

            return false;

        break;

    }

    case NO_RESOURCE:

    {

        add_status_line(404, error_404_title);

        add_headers(strlen(error_404_form));

        if (!add_content(error_404_form))

            return false;

        break;

    }

    case FORBIDDEN_REQUEST:

    {

        add_status_line(403, error_403_title);

        add_headers(strlen(error_403_form));

        if (!add_content(error_403_form))

            return false;

        break;

    }

    case DYNAMIC_REQUEST:

    {

        const std::string &body = m_dynamic_content;

        add_status_line(m_response_status, status_title(m_response_status));

        add_headers(static_cast<int>(body.size()));

        if (body.empty())

        {

            m_iv[0].iov_base = m_write_buf;

            m_iv[0].iov_len = m_write_idx;

            m_iv_count = 1;

            bytes_to_send = m_write_idx;

            return true;

        }

        m_iv[0].iov_base = m_write_buf;

        m_iv[0].iov_len = m_write_idx;

        m_iv[1].iov_base = (void *)body.data();

        m_iv[1].iov_len = body.size();

        m_file_address = const_cast<char *>(body.data());

        m_is_mmap = false;

        m_iv_count = 2;

        bytes_to_send = m_write_idx + body.size();

        return true;

    }

    case FILE_REQUEST:

    {

        add_status_line(200, ok_200_title);

        if (m_file_stat.st_size != 0)

        {

            add_headers(m_file_stat.st_size);

            m_iv[0].iov_base = m_write_buf;

            m_iv[0].iov_len = m_write_idx;

            m_iv[1].iov_base = m_file_address;

            m_iv[1].iov_len = m_file_stat.st_size;

            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_file_stat.st_size;

            return true;

        }

        else

        {

            const char *ok_string = "<html><body></body></html>";

            add_headers(strlen(ok_string));

            if (!add_content(ok_string))

                return false;

        }

        break;

    }

    case PHP_REQUEST:

    {

        add_status_line(200, ok_200_title);

        add_headers(static_cast<int>(m_php_content_size));

        if (m_php_content_size > 0)

        {

            m_iv[0].iov_base = m_write_buf;

            m_iv[0].iov_len = m_write_idx;

            m_iv[1].iov_base = m_php_content;

            m_iv[1].iov_len = m_php_content_size;

            m_iv_count = 2;

            bytes_to_send = m_write_idx + m_php_content_size;

            return true;

        }

        else

        {

            const char *ok_string = "<html><body></body></html>";

            add_headers(strlen(ok_string));

            if (!add_content(ok_string))

                return false;

        }

        break;

    }

    default:

        return false;

    }

    m_iv[0].iov_base = m_write_buf;

    m_iv[0].iov_len = m_write_idx;

    m_iv_count = 1;

    bytes_to_send = m_write_idx;

    return true;

}



/**
 * @brief HTTP连接处理主入口函数
 * @return true 处理完成，可以准备发送响应
 * @return false 需要更多数据，等待下次读取事件
 * 
 * 调用流程:
 *   1. 线程池工作线程调用该函数
 *   2. process_read()解析HTTP请求
 *   3. process_write()构建HTTP响应
 *   4. 修改epoll监听事件为EPOLLOUT
 *   5. 主线程检测到可写后调用write()
 * 
 * 特殊情况:
 *   - NO_REQUEST: 请求不完整，修改为EPOLLIN继续等待
 *   - 处理失败: 关闭连接
 */
bool http_conn::process()

{

    HTTP_CODE read_ret = process_read();
    LOG_INFO("process: process_read returned %d, m_read_idx=%ld, m_content_length=%ld, m_checked_idx=%ld, m_start_line=%ld", 
             read_ret, m_read_idx, m_content_length, m_checked_idx, m_start_line);
    // 请求不完整，继续等待更多数据
    if (read_ret == NO_REQUEST)

    {
        // 修改epoll事件为EPOLLIN，继续监听可读
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

        return false;  // 需要更多数据

    }
    // 请求计数+1（原子操作，线程安全）
    m_total_requests.fetch_add(1, std::memory_order_relaxed);
    // 构建HTTP响应
    bool write_ret = process_write(read_ret);

    if (!write_ret)

    {
        // 构建响应失败，关闭连接
        close_conn();
        return true;  // 处理完成（虽然是失败的）

    }
    // 修改epoll事件为EPOLLOUT，等待可写后发送响应
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
    return true;  // 处理完成

}
