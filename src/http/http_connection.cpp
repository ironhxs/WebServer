/**
 * @file http_connection.cpp
 * @brief HTTP连接处理实现
 * @details 实现请求解析、资源处理、响应构建以及辅助工具函数。
 */

#include "http_connection.h"



#include <mysql/mysql.h>

#include <algorithm>

#include <cctype>

#include <fstream>

#include <iomanip>

#include <sstream>

#include <vector>



/// 请求体最大允许大小（字节）
static const long kMaxBodySize = 200 * 1024 * 1024;


/// HTTP状态码对应的提示文本

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



/** @brief 去除字符串首尾空白 */
std::string trim(const std::string &value)

{

    size_t start = 0;

    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))

        ++start;

    size_t end = value.size();

    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))

        --end;

    return value.substr(start, end - start);

}



/** @brief 转换为小写副本 */
std::string to_lower_copy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return std::tolower(ch); });
    return value;
}

/** @brief 将16进制字符转为数值 */
int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return 10 + (ch - 'a');
    if (ch >= 'A' && ch <= 'F')
        return 10 + (ch - 'A');
    return -1;
}

/** @brief URL解码（%xx与+号） */
std::string url_decode(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == '+')
        {
            out.push_back(' ');
            continue;
        }
        if (value[i] == '%' && i + 2 < value.size())
        {
            int hi = hex_value(value[i + 1]);
            int lo = hex_value(value[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

/** @brief 从表单体中提取指定键的值 */
std::string get_form_value(const std::string &body, const std::string &key)
{
    std::string pattern = key + "=";
    size_t pos = body.find(pattern);
    if (pos == std::string::npos)
        return "";
    size_t start = pos + pattern.size();
    size_t end = body.find('&', start);
    std::string raw = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return url_decode(raw);
}

/** @brief 从X-Forwarded-For提取客户端IP */
std::string extract_forwarded_ip(const std::string &value)
{
    std::string trimmed = trim(value);
    if (trimmed.empty())
        return "";
    size_t comma = trimmed.find(',');
    std::string ip = comma == std::string::npos ? trimmed : trimmed.substr(0, comma);
    return trim(ip);
}

/** @brief HTML转义 */
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



/** @brief 规范化文件名，移除危险字符 */
std::string sanitize_filename(const std::string &value)

{

    std::string name;

    name.reserve(value.size());

    for (char ch : value)

    {

        if (ch == '/' || ch == '\\' || ch == ':' || ch == '|' || ch == '<' || ch == '>' || ch == '"')

            name.push_back('_');

        else if (std::iscntrl(static_cast<unsigned char>(ch)))

            name.push_back('_');

        else

            name.push_back(ch);

    }

    while (!name.empty() && name.front() == '.')

        name.erase(name.begin());

    if (name.empty())

        name = "upload.bin";

    return name;

}



/** @brief 格式化时间戳 */
std::string format_time(time_t timestamp)

{

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



/** @brief 根据状态码获取简短标题 */
const char *status_title(int status)

{

    switch (status)

    {

    case 200:

        return ok_200_title;

    case 302:

        return "Found";

    case 400:

        return error_400_title;

    case 403:

        return error_403_title;

    case 404:

        return error_404_title;

    case 413:

        return "Payload Too Large";

    case 500:

    default:

        return error_500_title;

    }

}



/** @brief 判断是否为图片扩展名 */
bool is_image_ext(const std::string &ext)

{

    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".webp" || ext == ".svg";

}



bool is_video_ext(const std::string &ext)
{
    return ext == ".mp4" || ext == ".webm" || ext == ".ogg";
}

/** @brief 从sockaddr_in提取IP字符串 */
std::string ip_from_addr(const sockaddr_in &addr)
{
    char buf[INET_ADDRSTRLEN] = {0};
    const char *res = inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    if (!res)
        return "";
    return std::string(buf);
}

/** @brief 判断是否为私有IPv4地址 */
bool is_private_ipv4(const std::string &ip)
{
    if (ip.rfind("10.", 0) == 0)
        return true;
    if (ip.rfind("127.", 0) == 0)
        return true;
    if (ip.rfind("192.168.", 0) == 0)
        return true;
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

/** @brief 规范化客户端IP（私有地址统一为local） */
std::string normalize_client_ip(const std::string &ip)
{
    if (ip.empty())
        return "";
    if (ip == "::1")
        return "local";
    if (is_private_ipv4(ip))
        return "local";
    if (ip.rfind("fe80:", 0) == 0)
        return "local";
    return ip;
}

/** @brief 从元数据文件加载用户上传列表 */
bool load_user_uploads(const std::string &doc_root, const std::string &username, std::vector<UploadItem> &items)
{
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
 */

void http_conn::initmysql_result(connection_pool *connPool)

{

    MYSQL *mysql = NULL;

    connectionRAII mysqlcon(&mysql, connPool);



    if (mysql_query(mysql, "SELECT username,passwd FROM user"))

    {

        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));

        return;

    }



    MYSQL_RES *result = mysql_store_result(mysql);

    if (!result)

        return;



    while (MYSQL_ROW row = mysql_fetch_row(result))

    {

        string temp1(row[0]);

        string temp2(row[1]);

        users[temp1] = temp2;

    }

    mysql_free_result(result);

}



/** @brief 设置fd为非阻塞
 * @return 原有flag
 */
int setnonblocking(int fd)

{

    int old_option = fcntl(fd, F_GETFL);

    int new_option = old_option | O_NONBLOCK;

    fcntl(fd, F_SETFL, new_option);

    return old_option;

}



// Register fd in epoll (optionally one-shot).

/** @brief 将fd注册到epoll */
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)

{

    epoll_event event;

    event.data.fd = fd;



    if (1 == TRIGMode)

        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;

    else

        event.events = EPOLLIN | EPOLLRDHUP;



    if (one_shot)

        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

    setnonblocking(fd);

}



// Remove fd from epoll and close it.

/** @brief 从epoll移除fd并关闭 */
void removefd(int epollfd, int fd)

{

    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);

    close(fd);

}



// Update epoll events for the fd.

/** @brief 修改fd的epoll监听事件 */
void modfd(int epollfd, int fd, int ev, int TRIGMode)

{

    epoll_event event;

    event.data.fd = fd;



    if (1 == TRIGMode)

        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;

    else

        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;



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


// Close the connection and update counters.

void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        // printf("close %d\n", m_sockfd);  // Debug output removed for performance
        if (!m_ip.empty())
        {
            m_ip_lock.lock();
            auto it = m_ip_counts.find(m_ip);
            if (it != m_ip_counts.end())
            {
                if (it->second <= 1)
                    m_ip_counts.erase(it);
                else
                    --(it->second);
            }
            m_ip_lock.unlock();
        }
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;
    m_ip = normalize_client_ip(ip_from_addr(addr));
    m_ip_from_header = false;
    if (!m_ip.empty())
    {
        m_ip_lock.lock();
        ++m_ip_counts[m_ip];
        m_unique_ips.insert(m_ip);
        m_ip_lock.unlock();
    }

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;


    // Store server config for this connection.

    doc_root = root;

    m_TRIGMode = TRIGMode;

    m_close_log = close_log;



    // Cache DB credentials for later use.

    strcpy(sql_user, user.c_str());

    strcpy(sql_passwd, passwd.c_str());

    strcpy(sql_name, sqlname.c_str());



    init();
}

void http_conn::update_client_ip(const std::string &ip)
{
    std::string normalized = normalize_client_ip(ip);
    if (normalized.empty() || normalized == m_ip)
        return;
    m_ip_lock.lock();
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
    m_ip = normalized;
    ++m_ip_counts[m_ip];
    m_unique_ips.insert(m_ip);
    m_ip_lock.unlock();
    m_ip_from_header = true;
}

void http_conn::init()
{
    if (m_start_time == 0)
        m_start_time = time(nullptr);
    mysql = NULL;

    m_php_content = NULL;

    m_php_content_size = 0;

    bytes_to_send = 0;

    bytes_have_send = 0;

    m_check_state = CHECK_STATE_REQUESTLINE;

    m_linger = false;

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



// Parse a single line in the read buffer (CRLF terminated).

http_conn::LINE_STATUS http_conn::parse_line()

{

    char temp;

    for (; m_checked_idx < m_read_idx; ++m_checked_idx)

    {

        temp = m_read_buf[m_checked_idx];

        if (temp == '\r')

        {

            if ((m_checked_idx + 1) == m_read_idx)

                return LINE_OPEN;

            else if (m_read_buf[m_checked_idx + 1] == '\n')

            {

                m_read_buf[m_checked_idx++] = '\0';

                m_read_buf[m_checked_idx++] = '\0';

                return LINE_OK;

            }

            return LINE_BAD;

        }

        else if (temp == '\n')

        {

            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')

            {

                m_read_buf[m_checked_idx - 1] = '\0';

                m_read_buf[m_checked_idx++] = '\0';

                return LINE_OK;

            }

            return LINE_BAD;

        }

    }

    return LINE_OPEN;

}



// Read once from the socket (LT/ET behavior depends on mode).

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



http_conn::HTTP_CODE http_conn::parse_request_line(char *text)

{

    m_url = strpbrk(text, " \t");

    if (!m_url)

    {

        return BAD_REQUEST;

    }

    *m_url++ = '\0';

    char *method = text;

    if (strcasecmp(method, "GET") == 0)

        m_method = GET;

    else if (strcasecmp(method, "POST") == 0)

    {

        m_method = POST;

        cgi = 1;

    }

    else

        return BAD_REQUEST;

    m_url += strspn(m_url, " \t");

    m_version = strpbrk(m_url, " \t");

    if (!m_version)

        return BAD_REQUEST;

    *m_version++ = '\0';

    m_version += strspn(m_version, " \t");

    if (strcasecmp(m_version, "HTTP/1.1") != 0)

        return BAD_REQUEST;

    if (strncasecmp(m_url, "http://", 7) == 0)

    {

        m_url += 7;

        m_url = strchr(m_url, '/');

    }



    if (strncasecmp(m_url, "https://", 8) == 0)

    {

        m_url += 8;

        m_url = strchr(m_url, '/');

    }



    if (!m_url || m_url[0] != '/')

        return BAD_REQUEST;

    // Default to index.html for root.

    if (strlen(m_url) == 1)

        strcat(m_url, "index.html");

    m_check_state = CHECK_STATE_HEADER;

    return NO_REQUEST;

}



http_conn::HTTP_CODE http_conn::parse_headers(char *text)

{

    if (text[0] == '\0')

    {

        if (m_content_length != 0)

        {

            m_check_state = CHECK_STATE_CONTENT;

            return NO_REQUEST;

        }

        return GET_REQUEST;

    }

    else if (strncasecmp(text, "Connection:", 11) == 0)

    {

        text += 11;

        text += strspn(text, " \t");

        if (strcasecmp(text, "keep-alive") == 0)

        {

            m_linger = true;

        }

    }

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



http_conn::HTTP_CODE http_conn::process_read()

{

    LINE_STATUS line_status = LINE_OK;

    HTTP_CODE ret = NO_REQUEST;

    char *text = 0;



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

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))

    {

        text = get_line();

        m_start_line = m_checked_idx;

        if (m_check_state != CHECK_STATE_CONTENT)
        {
            LOG_INFO("%s", text);
        }
        switch (m_check_state)

        {

        case CHECK_STATE_REQUESTLINE:

        {

            ret = parse_request_line(text);

            if (ret == BAD_REQUEST)

                return BAD_REQUEST;

            break;

        }

        case CHECK_STATE_HEADER:

        {

            ret = parse_headers(text);

            if (ret == BAD_REQUEST)

                return BAD_REQUEST;

            if (ret == DYNAMIC_REQUEST)

                return DYNAMIC_REQUEST;

            if (ret == GET_REQUEST)

                return do_request();

            break;

        }

        case CHECK_STATE_CONTENT:

        {

            ret = parse_content(text);

            if (ret == GET_REQUEST)

                return do_request();

            line_status = LINE_OPEN;

            break;

        }

        default:

            return INTERNAL_ERROR;

        }

    }

    return NO_REQUEST;

}



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


http_conn::HTTP_CODE http_conn::handle_status_json()
{
    time_t now = time(nullptr);
    if (m_start_time == 0)
        m_start_time = now;

    long long total = m_total_requests.load();
    long uptime = static_cast<long>(now - m_start_time);
    double qps = uptime > 0 ? static_cast<double>(total) / uptime : static_cast<double>(total);

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



http_conn::HTTP_CODE http_conn::handle_upload_request()

{

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

    if (m_method != POST)

        return fail("&#x5f53;&#x524d;&#x8bf7;&#x6c42;&#x65b9;&#x6cd5;&#x4e0d;&#x652f;&#x6301;&#x4e0a;&#x4f20;&#x3002;");

    if (m_username.empty())

        return fail("&#x672a;&#x68c0;&#x6d4b;&#x5230;&#x767b;&#x5f55;&#x7528;&#x6237;&#x3002;");

    if (!m_string || m_content_length <= 0)
        return fail("&#x672a;&#x68c0;&#x6d4b;&#x5230;&#x6709;&#x6548;&#x7684;&#x4e0a;&#x4f20;&#x5185;&#x5bb9;&#x3002;");

    const char *body_ptr = m_string;
    size_t body_len = static_cast<size_t>(m_content_length);
    const char *body_end = body_ptr + body_len;

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



http_conn::HTTP_CODE http_conn::handle_user_gallery_page()

{

    std::vector<UploadItem> items;

    load_user_uploads(doc_root, m_username, items);



    std::ostringstream body;

    body << R"HTML(<section class="panel" style="max-width: 980px; margin: 0 auto;">

<h2 style="font-size: 26px;">&#x6211;&#x7684;&#x56fe;&#x96c6;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x81ea;&#x52a8;&#x5c55;&#x793a;&#x4f60;&#x4e0a;&#x4f20;&#x7684;&#x56fe;&#x7247;&#x5185;&#x5bb9;&#x3002;</p>

<div class="grid" style="margin-top: 18px;">)HTML";



    bool has_image = false;

    for (const auto &item : items)

    {

        std::string name_lower = to_lower_copy(item.stored_name);

        size_t pos = name_lower.find_last_of('.');

        std::string ext = pos == std::string::npos ? "" : name_lower.substr(pos);

        if (!is_image_ext(ext))

            continue;

        has_image = true;

        std::string url = "/uploads/" + item.stored_name;

        body << R"HTML(<div class="card"><img src=")HTML" << url

             << R"HTML(" alt=")HTML" << html_escape(item.original_name)

             << R"HTML(" style="width:100%; border-radius: 18px; margin-bottom: 12px;"><h3>)HTML"

             << html_escape(item.original_name)

             << R"HTML(</h3><p style="margin-top: 8px;">&#x4e0a;&#x4f20;&#x65f6;&#x95f4;&#xff1a;)HTML"

             << format_time(static_cast<time_t>(item.timestamp))

             << R"HTML(</p></div>)HTML";

    }

    body << R"HTML(</div>)HTML";



    if (!has_image)

        body << R"HTML(<p style="margin-top: 16px;">&#x6682;&#x65f6;&#x8fd8;&#x6ca1;&#x6709;&#x56fe;&#x7247;&#xff0c;&#x53bb;&#x4e0a;&#x4f20;&#x4e00;&#x4e9b;&#x5427;&#x3002;</p>)HTML";



    body << R"HTML(<div class="actions" style="margin-top: 20px;">

<a class="btn primary" href="/pages/upload.html">&#x4e0a;&#x4f20;&#x4e2d;&#x5fc3;</a>



</div>

</section>)HTML";



    m_dynamic_content = build_page_shell("&#x6211;&#x7684;&#x56fe;&#x96c6;", body.str());

    m_dynamic_content_type = "text/html; charset=utf-8";

    m_response_status = 200;

    return DYNAMIC_REQUEST;

}



http_conn::HTTP_CODE http_conn::handle_user_video_page()

{

    std::vector<UploadItem> items;

    load_user_uploads(doc_root, m_username, items);



    std::ostringstream body;

    body << R"HTML(<section class="panel" style="max-width: 980px; margin: 0 auto;">

<h2 style="font-size: 26px;">&#x6211;&#x7684;&#x89c6;&#x9891;</h2>

<p style="margin-top: 8px; color: var(--muted);">&#x81ea;&#x52a8;&#x5c55;&#x793a;&#x4f60;&#x4e0a;&#x4f20;&#x7684;&#x89c6;&#x9891;&#x5185;&#x5bb9;&#x3002;</p>

<div class="grid" style="margin-top: 18px;">)HTML";



    bool has_video = false;

    for (const auto &item : items)

    {

        std::string name_lower = to_lower_copy(item.stored_name);

        size_t pos = name_lower.find_last_of('.');

        std::string ext = pos == std::string::npos ? "" : name_lower.substr(pos);

        if (!is_video_ext(ext))

            continue;

        has_video = true;

        std::string url = "/uploads/" + item.stored_name;

        body << R"HTML(<div class="card"><video src=")HTML" << url

             << R"HTML(" controls preload="metadata" style="width:100%; border-radius: 18px; margin-bottom: 12px;"></video><h3>)HTML"

             << html_escape(item.original_name)

             << R"HTML(</h3><p style="margin-top: 8px;">&#x4e0a;&#x4f20;&#x65f6;&#x95f4;&#xff1a;)HTML"

             << format_time(static_cast<time_t>(item.timestamp))

             << R"HTML(</p></div>)HTML";

    }

    body << R"HTML(</div>)HTML";



    if (!has_video)

        body << R"HTML(<p style="margin-top: 16px;">&#x6682;&#x65f6;&#x8fd8;&#x6ca1;&#x6709;&#x89c6;&#x9891;&#xff0c;&#x53bb;&#x4e0a;&#x4f20;&#x4e00;&#x4e9b;&#x5427;&#x3002;</p>)HTML";



    body << R"HTML(<div class="actions" style="margin-top: 20px;">

<a class="btn primary" href="/pages/upload.html">&#x4e0a;&#x4f20;&#x4e2d;&#x5fc3;</a>



</div>

</section>)HTML";



    m_dynamic_content = build_page_shell("&#x6211;&#x7684;&#x89c6;&#x9891;", body.str());

    m_dynamic_content_type = "text/html; charset=utf-8";

    m_response_status = 200;

    return DYNAMIC_REQUEST;

}



http_conn::HTTP_CODE http_conn::do_request()

{

    std::string url = m_url ? m_url : "/";
    if (url.empty())
        url = "/";
    url = url_decode(url);
    if (url.empty() || url[0] != '/')
        return BAD_REQUEST;
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



    snprintf(m_real_file, FILENAME_LEN, "%s%s", doc_root, url.c_str());

    if (stat(m_real_file, &m_file_stat) < 0)

        return render_not_found();

    if (!(m_file_stat.st_mode & S_IROTH))

        return FORBIDDEN_REQUEST;

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



    int fd = open(m_real_file, O_RDONLY);

    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    m_is_mmap = true;

    close(fd);

    return FILE_REQUEST;

}



bool http_conn::execute_php(const char *php_path)
{
    if (php_path == nullptr || strlen(php_path) == 0)
    {
        LOG_ERROR("Invalid PHP path provided.");
        return false;
    }

    char php_command[512];
    snprintf(php_command, sizeof(php_command), "php %s 2>&1", php_path);

    FILE *fp = popen(php_command, "r");
    if (fp == nullptr)
    {
        LOG_ERROR("Failed to execute PHP: %s", php_command);
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

    char buffer[4096];
    size_t read_size = 0;
    size_t total_size = 0;
    size_t capacity = sizeof(buffer);

    m_php_content = (char *)malloc(capacity);
    if (m_php_content == nullptr)
    {
        LOG_ERROR("Memory allocation failed for PHP output.");
        pclose(fp);
        return false;
    }

    while ((read_size = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    {
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
    pclose(fp);

    m_php_content[total_size] = '\0';
    m_php_content_size = total_size;
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
    m_is_mmap = false;
    return true;
}
void http_conn::unmap()

{

    if (m_is_mmap && m_file_address)

    {

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



bool http_conn::write()

{

    int temp = 0;



    if (bytes_to_send == 0)

    {

        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

        init();

        return true;

    }



    while (1)

    {

        temp = writev(m_sockfd, m_iv, m_iv_count);



        if (temp < 0)

        {

            if (errno == EAGAIN)

            {

                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);

                return true;

            }

            unmap();

            return false;

        }



        bytes_have_send += temp;

        bytes_to_send -= temp;

        if (static_cast<size_t>(bytes_have_send) >= m_iv[0].iov_len)
        {

            m_iv[0].iov_len = 0;

            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);

            m_iv[1].iov_len = bytes_to_send;

        }

        else

        {

            m_iv[0].iov_base = m_write_buf + bytes_have_send;

            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;

        }



        if (bytes_to_send <= 0)

        {

            unmap();

            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);



            if (m_linger)

            {

                init();

                return true;

            }

            else

            {

                return false;

            }

        }

    }

}



// Append formatted data to the write buffer.

bool http_conn::add_response(const char *format, ...)

{

    if (m_write_idx >= WRITE_BUFFER_SIZE)

        return false;

    va_list arg_list;

    va_start(arg_list, format);

    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))

    {

        va_end(arg_list);

        return false;

    }

    m_write_idx += len;

    va_end(arg_list);



    LOG_INFO("request:%s", m_write_buf);

    return true;

}



// Add status line.

bool http_conn::add_status_line(int status, const char *title)

{

    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);

}



bool http_conn::add_headers(int content_len)

{

    bool ok = add_content_length(content_len);

    if (!m_extra_headers.empty())

        ok = ok && add_response("%s", m_extra_headers.c_str());

    ok = ok && add_content_type();

    ok = ok && add_linger();

    ok = ok && add_blank_line();

    return ok;

}



// Add Content-Length header.

bool http_conn::add_content_length(int content_len)

{

    return add_response("Content-Length:%d\r\n", content_len);

}



// Add Content-Type header.

bool http_conn::add_content_type()

{

    const std::string &type = !m_dynamic_content_type.empty() ? m_dynamic_content_type : m_content_type;

    if (!type.empty())

        return add_response("Content-Type:%s\r\n", type.c_str());

    return add_response("Content-Type:%s\r\n", "text/html; charset=utf-8");

}



bool http_conn::add_linger()

{

    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");

}



// Add blank line after headers.

bool http_conn::add_blank_line()

{

    return add_response("%s", "\r\n");

}



// Add body content.

bool http_conn::add_content(const char *content)

{

    return add_response("%s", content);

}



// Build response headers and body for the given status.

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



bool http_conn::process()

{

    HTTP_CODE read_ret = process_read();
    LOG_INFO("process: process_read returned %d, m_read_idx=%ld, m_content_length=%ld, m_checked_idx=%ld, m_start_line=%ld", 
             read_ret, m_read_idx, m_content_length, m_checked_idx, m_start_line);

    if (read_ret == NO_REQUEST)

    {

        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

        return false;  // 需要更多数据

    }

    m_total_requests.fetch_add(1, std::memory_order_relaxed);

    bool write_ret = process_write(read_ret);

    if (!write_ret)

    {

        close_conn();
        return true;  // 处理完成（虽然是失败的）

    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
    return true;  // 处理完成

}
