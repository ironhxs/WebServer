# 项目结构说明文档



## 目录结构



```

WebServer/

│

├──  main.cpp                 # 主程序入口

├──  config.h/cpp             # 配置解析模块

├──  webserver.h/cpp          # 服务器核心逻辑

├──  makefile                 # 构建系统（已优化）

├──  build.sh                 # 编译脚本

├──  manage.sh                # 服务器管理脚本

│

├──  http/                    # HTTP协议处理模块

│   ├── http_connection.h/cpp    # HTTP连接类

│   └── README.md               # HTTP模块说明

│

├──  threadpool/              # 线程池模块

│   ├── threadpool.h            # 线程池模板类

│   └── README.md               # 线程池说明

│

├──  CGImysql/                # 数据库连接池模块

│   ├── database_pool.h/cpp     # 连接池实现

│   └── README.md               # 数据库模块说明

│

├──  log/                     # 日志系统模块

│   ├── log.h/cpp               # 日志类实现

│   ├── blocking_queue.h        # 阻塞队列（异步日志）

│   └── README.md               # 日志系统说明

│

├──  timer/                   # 定时器模块

│   ├── timer_list.h/cpp        # 定时器链表

│   └── README.md               # 定时器说明

│

├──  lock/                    # 线程同步模块

│   ├── thread_sync.h           # 锁、信号量、条件变量

│   └── README.md               # 同步机制说明

│

├──  resources/webroot/                    # 网站资源目录

│   ├── index.html               # 首页
  │   ├── phpinfo.php            # PHP 示例页面

│   ├── pages/log.html              # 登录页面

│   ├── pages/register.html           # 注册页面
  │   ├── pages/upload.html             # 上传页面
  │   ├── pages/status.html             # 监控页面
  │   └── uploads/                      # 上传文件保存目录

│   ├── pages/welcome.html            # 欢迎页面

│   ├── 404.html                # 404错误页

│   └── assets/                   # 静态资源

│       ├── css/          # 主页

│       ├── media/           # 样式文件

│       ├── images/             # 图片资源

│       ├── js/                 # JavaScript文件

│       └── plugins/            # 第三方插件

│

├──  tests/benchmark/           # 压力测试工具

│   └── webbench-1.5/           # Webbench压测工具

│

├──  README.md                # 项目完整文档

├──  QUICKSTART.md            # 快速入门指南

├──  CODE_COMMENTS.md         # 代码注释说明

├──  PROJECT_STRUCTURE.md     # 本文件

│

├──  setup_db.sql             # 数据库初始化脚本

├──  server.conf.example      # 配置文件示例

├──  .gitignore               # Git忽略文件

│

└──  server                   # 编译生成的可执行文件

    └── *.log                   # 运行日志



```



## 核心模块说明



### 1. 主程序模块 (main.cpp)

**职责：** 程序入口，初始化所有模块并启动服务器

**关键函数：**

- `main()` - 主函数，执行初始化流程



### 2. 配置模块 (config.h/cpp)

**职责：** 解析命令行参数，管理服务器配置

**主要类：** `Config`

**支持参数：**

- `-p` 端口号

- `-l` 日志模式

- `-m` 触发模式

- `-t` 线程数

- `-s` 连接池大小

- `-o` 优雅关闭

- `-c` 关闭日志

- `-a` 并发模型



### 3. 服务器核心 (webserver.h/cpp)

**职责：** 服务器主要逻辑，事件循环，连接管理

**主要类：** `WebServer`

**核心方法：**

- `init()` - 初始化参数

- `log_write()` - 初始化日志

- `sql_pool()` - 创建数据库连接池

- `thread_pool()` - 创建线程池

- `eventListen()` - 开始监听

- `eventLoop()` - 事件循环主函数



### 4. HTTP处理模块 (http/)

**职责：** 解析HTTP请求，生成HTTP响应

**主要类：** `HttpConnection`

**功能：**

- HTTP请求解析（请求行、请求头、请求体）

- 静态资源服务（零拷贝mmap）

- CGI支持（PHP执行）

- Keep-Alive连接管理

- 用户登录/注册



### 5. 线程池模块 (threadpool/)

**职责：** 管理工作线程，并发处理请求

**主要类：** `threadpool<T>`

**特性：**

- 预创建固定数量线程

- 请求队列（FIFO）

- 信号量+互斥锁同步

- 支持Reactor/Proactor模式



### 6. 数据库连接池 (CGImysql/)

**职责：** 管理MySQL连接，提供连接复用

**主要类：** `connection_pool`

**功能：**

- 连接预创建

- 连接获取/释放

- 单例模式

- 线程安全



### 7. 日志系统 (log/)

**职责：** 记录服务器运行信息，支持异步写入

**主要类：** `Log`

**特性：**

- 同步/异步日志

- 日志分割（按日期/大小）

- 阻塞队列（异步模式）

- 日志级别控制



### 8. 定时器模块 (timer/)

**职责：** 管理连接超时，自动清理非活动连接

**主要类：** `util_timer`

**实现：** 升序链表

**功能：**

- 连接超时检测

- 定时任务调度

- 非活动连接清理



### 9. 线程同步模块 (lock/)

**职责：** 提供线程同步原语

**主要类：**

- `locker` - 互斥锁（RAII封装）

- `cond` - 条件变量

- `sem` - 信号量



## 数据流图



```

客户端请求

    ↓

监听Socket (主线程)

    ↓

Epoll事件通知

    ↓

主线程accept() / 工作线程recv() [根据模型]

    ↓

请求队列 (线程池)

    ↓

工作线程获取任务

    ↓

HTTP解析 → 业务处理 → 响应生成

    |              |

    |              ↓

    |         数据库连接池

    |              |

    ↓              ↓

发送响应 ← 日志记录

    ↓

客户端接收

```



## 请求处理流程



### Proactor模式（actor_model=0）

```

1. 主线程epoll_wait()监听事件

2. 主线程accept()新连接

3. 主线程recv()读取请求数据

4. 主线程将任务放入请求队列

5. 工作线程从队列取任务

6. 工作线程解析HTTP请求

7. 工作线程处理业务逻辑

8. 工作线程send()发送响应

```



### Reactor模式（actor_model=1）

```

1. 主线程epoll_wait()监听事件

2. 主线程accept()新连接

3. 主线程将任务放入请求队列

4. 工作线程从队列取任务

5. 工作线程recv()读取请求

6. 工作线程解析HTTP请求

7. 工作线程处理业务逻辑

8. 工作线程send()发送响应

```



## 性能优化要点



### 1. I/O多路复用

- 使用Epoll（Linux）

- 支持LT/ET触发模式

- 非阻塞Socket



### 2. 并发处理

- 线程池（避免频繁创建销毁）

- 请求队列（解耦）

- 信号量同步（高效）



### 3. 连接管理

- 数据库连接池（复用连接）

- Keep-Alive（HTTP持久连接）

- 定时器（超时清理）



### 4. 零拷贝技术

- mmap内存映射（静态文件）

- writev聚集写（响应头+体）



### 5. 日志优化

- 异步日志（不阻塞工作线程）

- 日志分割（避免文件过大）



## 开发规范



### 代码风格

- 使用C++11标准

- 遵循Google C++风格指南

- 函数不超过50行

- 文件不超过1000行



### 命名规范

- 类名：大驼峰（如 `WebServer`）

- 函数名：小写+下划线（如 `event_loop`）

- 成员变量：`m_`前缀（如 `m_port`）

- 常量：全大写+下划线（如 `MAX_FD`）



### 注释规范

- 使用Doxygen格式

- 每个函数都有注释

- 复杂逻辑添加注释

- 注释说明"为什么"而不只是"做什么"



## 构建系统



### Makefile目标

```bash

make            # 编译（默认DEBUG模式）

make DEBUG=0    # 发布版本编译

make clean      # 清理构建文件

make distclean  # 深度清理（包括日志）

make rebuild    # 重新编译

make run        # 编译并运行

make start      # 后台运行

make stop       # 停止服务器

make status     # 查看状态

make log        # 查看日志

make deps       # 安装依赖

make help       # 显示帮助

```



## 文档索引



- [README.md](Doc.md) - 完整项目文档

- [QUICKSTART.md](QUICKSTART.md) - 快速入门

- [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) - 本文件



---



**最后更新：** 2026-01-09  

**文档版本：** v2.0

