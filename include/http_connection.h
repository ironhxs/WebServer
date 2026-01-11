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

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 64 * 1024;
    static const int WRITE_BUFFER_SIZE = 8 * 1024;
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        DYNAMIC_REQUEST,
        PHP_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true);
    void process();
    bool read_once();
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;
    bool execute_php(const char *);


private:
    void init();
    HTTP_CODE process_read();
    bool process_write(HTTP_CODE ret);
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    HTTP_CODE handle_status_json();
    HTTP_CODE handle_welcome_page();
    HTTP_CODE handle_upload_request();
    HTTP_CODE handle_upload_list();
    bool user_owns_upload(const std::string &owner, const std::string &stored_name) const;
    HTTP_CODE handle_user_gallery_page();
    HTTP_CODE handle_user_video_page();
    void update_client_ip(const std::string &ip);
    std::string get_cookie_value(const std::string &key) const;
    std::string build_page_shell(const std::string &title, const std::string &body) const;
    char *get_line() { return m_read_buf.data() + m_start_line; };
    LINE_STATUS parse_line();
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    static locker m_ip_lock;
    static std::unordered_map<std::string, int> m_ip_counts;
    static std::unordered_set<std::string> m_unique_ips;
    static std::atomic<long long> m_total_requests;
    static time_t m_start_time;
    MYSQL *mysql;
    int m_state;  // 0 = read, 1 = write

private:
    int m_sockfd;
    char *m_php_content;
    size_t m_php_content_size;
    sockaddr_in m_address;
    std::vector<char> m_read_buf;
    long m_read_idx;
    long m_checked_idx;
    int m_start_line;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;
    CHECK_STATE m_check_state;
    METHOD m_method;
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    long m_content_length;
    bool m_linger;
    char *m_file_address;
    bool m_is_mmap;
    std::string m_content_type;
    std::string m_cookie;
    std::string m_dynamic_content;
    std::string m_dynamic_content_type;
    std::string m_username;
    std::string m_extra_headers;
    bool m_ip_from_header;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        // Whether POST handling is enabled
    char *m_string; // Request body buffer
    int bytes_to_send;
    int bytes_have_send;
    int m_response_status;
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;
    std::string m_ip;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
