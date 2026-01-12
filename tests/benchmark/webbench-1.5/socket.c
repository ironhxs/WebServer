/* $Id: socket.c 1.1 1995/01/01 07:11:14 cthuang Exp $
 *
 * This module has been modified by Radim Kolar for OS/2 emx
 */

/***********************************************************************
  module:       socket.c
  program:      popclient
  SCCS ID:      @(#)socket.c    1.5  4/1/94
  programmer:   Virginia Tech Computing Center
  compiler:     DEC RISC C compiler (Ultrix 4.1)
  environment:  DEC Ultrix 4.3 
  description:  UNIX sockets code.
  
  中文说明:
  提供TCP客户端连接功能，封装socket创建和连接过程。
  支持IP地址和域名两种主机格式。
 ***********************************************************************/
 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/**
 * @brief 创建TCP客户端连接
 * @param host 目标主机 (IP地址或域名)
 * @param clientPort 目标端口号
 * @return 成功返回socket fd, 失败返回-1
 * 
 * 工作流程:
 * 1. 初始化sockaddr_in地址结构
 * 2. 解析主机地址 (支持IP和域名)
 * 3. 创建socket
 * 4. 连接到服务器
 * 
 * 使用的系统调用:
 * - inet_addr(): 将点分十进制IP转为网络字节序
 * - gethostbyname(): DNS域名解析
 * - socket(): 创建套接字
 * - connect(): 建立TCP连接
 * - htons(): 主机字节序转网络字节序(端口)
 */
int Socket(const char *host, int clientPort)
{
    int sock;
    unsigned long inaddr;
    struct sockaddr_in ad;  /* 服务器地址结构 */
    struct hostent *hp;     /* 主机信息结构 */
    
    /* 初始化地址结构 */
    memset(&ad, 0, sizeof(ad));
    ad.sin_family = AF_INET;  /* IPv4协议族 */

    /**
     * inet_addr() - 将点分十进制IP转为网络字节序整数
     * @param cp: 点分十进制字符串 (如 "192.168.1.1")
     * @return: 成功返回网络字节序IP, 失败返回INADDR_NONE
     */
    inaddr = inet_addr(host);
    if (inaddr != INADDR_NONE)
        /* host是IP地址格式 */
        memcpy(&ad.sin_addr, &inaddr, sizeof(inaddr));
    else
    {
        /**
         * gethostbyname() - DNS域名解析
         * @param name: 域名字符串 (如 "www.example.com")
         * @return: 成功返回hostent结构指针, 失败返回NULL
         * 
         * hostent结构包含:
         * - h_name: 官方主机名
         * - h_addr: 主机IP地址
         * - h_length: 地址长度
         */
        hp = gethostbyname(host);
        if (hp == NULL)
            return -1;  /* DNS解析失败 */
        memcpy(&ad.sin_addr, hp->h_addr, hp->h_length);
    }
    
    /**
     * htons() - Host TO Network Short
     * 将16位整数从主机字节序转为网络字节序(大端)
     * 网络传输使用大端序，x86使用小端序，需要转换
     */
    ad.sin_port = htons(clientPort);
    
    /**
     * socket() - 创建套接字
     * @param domain: AF_INET = IPv4
     * @param type: SOCK_STREAM = TCP流式套接字
     * @param protocol: 0 = 自动选择协议
     * @return: 成功返回socket fd, 失败返回-1
     */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        return sock;
    
    /**
     * connect() - 建立TCP连接
     * @param sockfd: socket文件描述符
     * @param addr: 目标地址结构
     * @param addrlen: 地址结构长度
     * @return: 成功返回0, 失败返回-1
     * 
     * 这是TCP三次握手的客户端发起方
     */
    if (connect(sock, (struct sockaddr *)&ad, sizeof(ad)) < 0)
        return -1;
    return sock;
}

