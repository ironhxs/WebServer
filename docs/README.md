#  高性能 Web 服务器项目



![C++](https://img.shields.io/badge/C++-11-blue.svg)

![MySQL](https://img.shields.io/badge/MySQL-8.0-orange.svg)

![License](https://img.shields.io/badge/license-MIT-green.svg)



一个基于 **C++11** 实现的高性能 Web 服务器，支持高并发处理、数据库连接池、异步日志系统等企业级特性。



## 文档导航



- [快速开始](QUICKSTART.md) - 5分钟快速上手

- [项目结构](PROJECT_STRUCTURE.md) - 详细的项目目录说明

- [代码注释](CODE_COMMENTS.md) - 代码注释文档索引

- [优化总结](OPTIMIZATION_SUMMARY.md) - 项目优化详细报告

- [模块说明](#模块详解) - 各模块技术细节



---



## 项目特性



### 核心功能

- **高并发处理**：使用 Epoll I/O 多路复用 + 线程池处理并发请求

- **数据库连接池**：MySQL 连接池管理，支持用户登录/注册

- **异步日志系统**：支持同步/异步日志记录，保证高性能

- **定时器管理**：非活动连接自动断开，防止资源浪费

- **HTTP 协议支持**：完整的 HTTP 请求解析与响应

- **PHP-CGI 支持**：支持执行 PHP 脚本（可选）

- **静态资源服务**：高效的静态文件服务



### 技术亮点

- **Reactor/Proactor 模式**：支持两种并发模型切换

- **线程池**：预创建线程，避免频繁创建销毁开销

- **非阻塞 Socket**：ET/LT 触发模式可选

- **内存映射**：零拷贝技术加速文件传输

- **优雅关闭**：支持 SO_LINGER 选项



## 技术栈



| 技术 | 说明 |

|------|------|

| C++11 | 核心编程语言 |

| Epoll | Linux 高性能 I/O 多路复用 |

| MySQL 8.0 | 数据库存储 |

| Pthread | POSIX 多线程库 |

| CGI | PHP 脚本执行支持 |



## 快速开始



### 1. 安装依赖

```bash

# Ubuntu / Debian

sudo apt-get update

sudo apt-get install -y build-essential libmysqlclient-dev mysql-server



# CentOS / RHEL

sudo yum install -y gcc-c++ mysql-devel mysql-server

```



### 2. 初始化数据库

```bash

# 启动 MySQL

sudo systemctl start mysql



# 创建数据库和表

mysql -u root -p < setup_db.sql

```



### 3. 编译运行

```bash

# 编译（Debug模式）

make



# 或编译（Release模式）

make DEBUG=0



# 启动服务器

./manage.sh start



# 查看状态

./manage.sh status

```



### 4. 访问测试

```bash

# 浏览器访问

http://localhost:9006/



# 或使用 curl

curl http://localhost:9006/

```



## 使用指南



### 编译命令

```bash

make              # 默认编译（Debug模式）

make DEBUG=0      # Release模式编译

make clean        # 清理构建文件

make rebuild      # 重新构建

make help         # 显示帮助信息

```



### 服务器管理

```bash

./manage.sh start     # 启动服务器

./manage.sh stop      # 停止服务器

./manage.sh restart   # 重启服务器

./manage.sh status    # 查看状态

./manage.sh log       # 查看日志

```



### 命令行参数

```bash

./server [选项]



选项：

  -p <port>      监听端口号（默认：9006）

  -l <mode>      日志模式：0=同步 1=异步（默认：0）

  -m <mode>      触发模式：0=LT+LT 1=LT+ET 2=ET+LT 3=ET+ET（默认：0）

  -o <linger>    优雅关闭：0=不使用 1=使用（默认：0）

  -s <num>       数据库连接池大小（默认：8）

  -t <num>       线程池线程数（默认：8）

  -c <log>       关闭日志：0=开启 1=关闭（默认：0）

  -a <model>     并发模型：0=Proactor 1=Reactor（默认：0）



示例：

  ./server -p 8080 -l 1 -m 3 -t 16

```



## 项目结构



```

WebServer/

├── main.cpp                    # 程序入口

├── config.h/cpp                # 配置管理

├── webserver.h/cpp             # Web服务器核心

├── makefile                    # 构建系统

├── manage.sh                   # 服务器管理脚本

├── server.conf.example         # 配置文件模板

├── setup_db.sql                # 数据库初始化脚本

├── .gitignore                  # Git忽略规则

│

├── docs/                       #  项目文档

│   ├── README.md               # 本文档

│   ├── QUICKSTART.md           # 快速开始指南

│   ├── PROJECT_STRUCTURE.md    # 项目结构详解

│   ├── CODE_COMMENTS.md        # 代码注释索引

│   └── OPTIMIZATION_SUMMARY.md # 优化总结报告

│

├── threadpool/                 # 线程池模块

│   └── threadpool.h            # 模板类实现

│

├── http/                       # HTTP处理模块

│   ├── http_connection.h/cpp    # HTTP连接类

│   └── README.md               # 模块说明

│

├── log/                        # 日志系统

│   ├── log.h/cpp               # 日志类实现

│   ├── blocking_queue.h        # 阻塞队列

│   └── README.md               # 模块说明

│

├── CGImysql/                   # 数据库连接池

│   ├── database_pool.h/cpp

│   └── README.md               # 模块说明

│

├── timer/                      # 定时器模块

│   ├── timer_list.h/cpp        # 定时器链表

│   └── README.md               # 模块说明

│

├── lock/                       # 同步机制封装

│   ├── thread_sync.h           # 互斥锁、条件变量、信号量

│   └── README.md               # 模块说明

│

├── resources/webroot/                       # 网站根目录

│   ├── index.html              # 主页

│   ├── *.html                  # 各种页面

│   └── html/                   # 静态资源

│

└── tests/benchmark/              # 压力测试工具

    └── webbench-1.5/           # Webbench工具

```



## 模块详解



### 1. 线程池（threadpool/）

**设计模式：半同步/半反应堆**



- 使用工作队列解耦主线程和工作线程

- 主线程负责监听事件，插入任务到队列

- 工作线程竞争获取任务并执行

- 支持 Reactor 和 Proactor 两种并发模型



**并发模型对比：**



| 模式 | 主线程 | 工作线程 | 适用场景 |

|------|--------|----------|----------|

| Proactor | 读写数据 + 监听事件 | 处理业务逻辑 | I/O密集型 |

| Reactor | 只监听事件 | 读写数据 + 业务逻辑 | 高并发短连接 |



**关键代码：**

```cpp

template <typename T>

class threadpool {

    bool append(T *request, int state);      // Reactor模式添加任务

    bool append_p(T *request);               // Proactor模式添加任务

    void run();                              // 工作线程主循环

    

private:

    std::list<T *> m_workqueue;              // 任务队列

    locker m_queuelocker;                    // 队列互斥锁

    sem m_queuestat;                         // 信号量

};

```



### 2. HTTP处理（http/）

**状态机设计：主从状态机协作**



- **从状态机**：按行/按块读取数据，解析HTTP请求

- **主状态机**：根据从状态机结果，决定响应或继续读取



**HTTP请求处理流程：**

1. 读取请求行（方法、URL、版本）

2. 解析请求头（Host、Connection等）

3. 解析请求体（POST数据）

4. 处理请求（文件服务/数据库查询/CGI执行）

5. 生成响应（状态行 + 响应头 + 响应体）

6. 发送响应（内存映射零拷贝）



**支持的功能：**

- GET/POST 方法

- Keep-Alive 长连接

- 用户登录/注册（数据库）

- 静态文件服务

- PHP-CGI 脚本执行



### 3. 日志系统（log/）

**设计：异步日志 + 阻塞队列**



- **同步模式**：直接写入文件（简单，有阻塞）

- **异步模式**：生产者-消费者模型（高性能，无阻塞）

  - 日志写入阻塞队列

  - 专门线程从队列取出并写文件

  - 队列满时阻塞，避免内存爆炸



**日志功能：**

- 日志分级（DEBUG/INFO/WARN/ERROR）

- 按日期自动切割日志文件

- 按大小自动分割日志文件

- 线程安全的日志写入



**使用示例：**

```cpp

LOG_INFO("Server started on port %d", port);

LOG_ERROR("Failed to bind socket: %s", strerror(errno));

```



### 4. 数据库连接池（CGImysql/）

**设计：预创建连接 + RAII管理**



- 启动时创建N个MySQL连接

- 使用信号量控制连接数量

- 用完后归还到池中，避免频繁创建销毁

- RAII封装：connectionRAII 自动获取和释放连接



**性能优势：**

- 减少连接建立时间（3次握手 + 认证）

- 避免连接频繁创建销毁开销

- 限制最大连接数，保护数据库



**使用方式：**

```cpp

connectionRAII mysqlcon(&mysql, connPool);  // 自动获取连接

// 使用 mysql 进行数据库操作

// 析构时自动归还连接

```



### 5. 定时器（timer/）

**设计：升序双向链表**



- 每个连接对应一个定时器节点

- 按超时时间排序，最早超时的在链表头

- 定期检查链表头，清理超时连接



**功能：**

- 检测非活动连接（超时无数据）

- 自动关闭超时连接，释放资源

- 防止连接资源耗尽



**优化点：**

- 有事件时延长定时器（刷新超时时间）

- 避免频繁插入删除（调整节点位置）



### 6. 同步机制（lock/）

**封装POSIX线程同步原语**



```cpp

class locker       // 互斥锁（RAII）

class cond         // 条件变量

class sem          // 信号量

```



**使用场景：**

- `locker`：保护共享数据（任务队列、连接池）

- `cond`：线程等待和通知（可选，当前未使用）

- `sem`：计数资源（任务数量、连接数量）



## 性能测试



### Webbench 压力测试

```bash

cd tests/benchmark/webbench-1.5

make

./webbench -c 10000 -t 10 http://localhost:9006/

```



**测试指标：**

- 并发连接数：10000

- 测试时长：10秒

- QPS：每秒请求数

- 成功率：请求成功百分比



### 性能调优建议

```cpp

// 1. 线程数 = CPU核心数 * 2

thread_num = 8;



// 2. 连接池大小

sql_num = 8~16;



// 3. 使用异步日志

LOGWrite = 1;



// 4. 使用ET模式

TRIGMode = 3;  // ET+ET



// 5. 开启优雅关闭

OPT_LINGER = 1;

```



## 开发计划



- [ ] 添加HTTPS支持（OpenSSL）

- [ ] 实现WebSocket协议

- [ ] 支持HTTP/2.0

- [ ] 添加配置文件解析

- [ ] 实现热加载配置

- [ ] 添加单元测试

- [ ] 性能监控接口

- [ ] Docker容器化



## 贡献指南



欢迎提交 Issue 和 Pull Request！



1. Fork 本仓库

2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)

3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)

4. 推送到分支 (`git push origin feature/AmazingFeature`)

5. 提交 Pull Request



## 许可证



本项目采用 MIT 许可证 - 详见 [LICENSE](LICENSE) 文件



## 致谢



- 感谢所有开源社区的贡献者

- 参考了多个优秀的C++服务器项目



## 联系方式



- 项目地址：https://github.com/your-repo/WebServer

- 问题反馈：https://github.com/your-repo/WebServer/issues



---



 如果觉得项目不错，欢迎 Star 支持！

## 功能入口

- **首页**：http://localhost:9006/
- **演示路线**：http://localhost:9006/demo.html
- **登录**：http://localhost:9006/pages/log.html
- **注册**：http://localhost:9006/pages/register.html
- **上传文件**：http://localhost:9006/pages/upload.html
- **我的上传**：http://localhost:9006/uploads/list（图集和视频已合并）
- **监控页面**：http://localhost:9006/pages/status.html
- **PHP 示例**：http://localhost:9006/phpinfo.php

## 内网穿透（Cloudflare 推荐）

```bash
# 安装 cloudflared（Ubuntu/WSL）
curl -L -o cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb
sudo dpkg -i cloudflared.deb

# 启动 HTTP 穿透
cloudflared tunnel --url http://localhost:9006 --protocol http2
# If QUIC times out, keep --protocol http2 (TCP). Quick Tunnel does not need cert.
```

### 内网穿透（Ngrok 可选）

```bash
ngrok config add-authtoken <YOUR_TOKEN>
ngrok http 9006
```

## 功能测试建议

```bash
curl -s http://localhost:9006/status.json
curl -F "file=@/path/to/file.jpg" http://localhost:9006/upload
curl http://localhost:9006/phpinfo.php
```

## 内网穿透脚本

```bash
./scripts/tunnel_ngrok.sh 9006
./scripts/tunnel_cloudflared.sh 9006 --protocol http2
```

## 压力测试脚本

```bash
./scripts/run_webbench.sh http://localhost:9006/ 10000 10
```

