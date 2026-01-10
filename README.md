# WebServer - 高性能C++服务器

![C++](https://img.shields.io/badge/C++-11-blue.svg)
![MySQL](https://img.shields.io/badge/MySQL-8.0-orange.svg)
![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)

基于 C++11 实现的企业级高性能 Web 服务器，采用 Epoll + 线程池 + 数据库连接池 + 异步日志架构。

## ? 快速开始

```bash
# 1. 安装依赖
make install-deps

# 2. 初始化数据库
mysql -u root -p < config/setup_db.sql

# 3. 编译运行
make
make start

# 4. 访问测试
curl http://localhost:9006/
```

## ? 完整文档

详细文档请查看 **[docs/README.md](docs/README.md)**

## ? 核心特性

- ? **Epoll + 线程池**：支持 10000+ 并发连接
- ? **Reactor/Proactor**：双并发模型支持
- ? **数据库连接池**：高效的 MySQL 连接管理
- ? **异步日志系统**：非阻塞日志记录
- ? **定时器管理**：自动清理超时连接
- ? **HTTP/1.1**：完整协议支持，Keep-Alive 长连接

## ? 项目结构

```
WebServer/
├── include/                    # 头文件
│   ├── webserver.h            # 服务器核心
│   ├── config.h               # 配置管理
│   ├── http_conn.h            # HTTP连接处理
│   ├── log.h                  # 日志系统
│   ├── threadpool.h           # 线程池
│   ├── sql_connection_pool.h  # 数据库连接池
│   ├── lst_timer.h            # 定时器
│   ├── locker.h               # 同步机制
│   └── block_queue.h          # 阻塞队列
│
├── src/                       # 源文件
│   ├── core/                  # 核心模块
│   │   ├── main.cpp           # 程序入口
│   │   ├── webserver.cpp      # 服务器实现
│   │   └── config.cpp         # 配置实现
│   ├── http/                  # HTTP模块
│   │   └── http_conn.cpp
│   ├── log/                   # 日志模块
│   │   └── log.cpp
│   ├── timer/                 # 定时器模块
│   │   └── lst_timer.cpp
│   └── database/              # 数据库模块
│       └── sql_connection_pool.cpp
│
├── build/                     # 构建输出（自动生成）
│   ├── obj/                   # 目标文件
│   └── deps/                  # 依赖文件
│
├── bin/                       # 可执行文件（自动生成）
│   └── webserver
│
├── config/                    # 配置文件
│   ├── setup_db.sql           # 数据库初始化
│   └── server.conf.example    # 配置模板
│
├── scripts/                   # 脚本工具
│   └── manage.sh              # 服务器管理脚本
│
├── resources/                 # 资源文件
│   └── webroot/               # 网站根目录
│       ├── index.html
│       └── ...
│
├── tests/                     # 测试
│   └── benchmark/             # 性能测试
│       └── webbench-1.5/      # Webbench工具
│
├── docs/                      # 文档
│   ├── README.md              # 完整文档
│   ├── QUICKSTART.md          # 快速开始
│   └── ...
│
├── Makefile                   # 构建系统
├── .gitignore                 # Git忽略规则
└── README.md                  # 本文件
```

## ?? 常用命令

### 编译
```bash
make              # Debug 模式
make DEBUG=0      # Release 模式
make clean        # 清理
make rebuild      # 重新构建
```

### 运行
```bash
make run          # 前台运行
make start        # 后台启动
make stop         # 停止服务
make status       # 查看状态
```

### 测试
```bash
make test         # 性能测试（需要服务器运行）
```

### 自定义参数
```bash
./bin/webserver -p 8080 -l 1 -m 3 -t 16

  -p <port>   端口号（默认9006）
  -l <mode>   日志：0=同步 1=异步
  -m <mode>   触发：0=LT+LT 1=LT+ET 2=ET+LT 3=ET+ET
  -t <num>    线程数（默认8）
  -s <num>    数据库连接数（默认8）
  -a <model>  模型：0=Proactor 1=Reactor
```

## ? 性能指标

```bash
# Webbench 压力测试结果
并发：10000 clients
时长：10 seconds
成功率：100%
QPS：~50000 requests/sec
```

## ? 技术栈

- **C++11**：核心语言
- **Epoll**：I/O 多路复用（ET/LT）
- **线程池**：Reactor/Proactor 模式
- **MySQL**：数据库 + 连接池
- **异步日志**：队列 + 后台线程
- **HTTP/1.1**：完整协议实现

## ?? 配置数据库

编辑 `src/core/webserver.cpp`：
```cpp
user = "root";
passwd = "your_password";
databasename = "xhrdb";
```

## ? 开发计划

- [ ] HTTPS 支持
- [ ] HTTP/2.0
- [ ] WebSocket
- [ ] 配置文件解析
- [ ] Docker 容器化

## ? 许可证

MIT License

---

**完整文档**：[docs/README.md](docs/README.md)


## ? 快速开始

```bash
# 1. 安装依赖
sudo apt-get install -y build-essential libmysqlclient-dev mysql-server

# 2. 初始化数据库
mysql -u root -p < docs/setup_db.sql

# 3. 编译运行
make
./manage.sh start

# 4. 访问测试
curl http://localhost:9006/
```

## ? 完整文档

详细文档请查看 **[docs/README.md](docs/README.md)**

- [快速开始指南](docs/QUICKSTART.md) - 5分钟上手教程
- [项目结构说明](docs/PROJECT_STRUCTURE.md) - 目录结构详解

## ? 核心特性

- ? **Epoll + 线程池**：高并发请求处理（支持10000+并发）
- ? **Reactor/Proactor**：双并发模型支持
- ? **数据库连接池**：MySQL连接复用，减少开销
- ? **异步日志系统**：高性能日志记录
- ? **定时器管理**：自动清理超时连接
- ? **HTTP/1.1**：完整协议支持，Keep-Alive长连接

## ?? 常用命令

### 编译
```bash
make              # Debug模式编译
make DEBUG=0      # Release模式编译
make clean        # 清理
make rebuild      # 重新构建
make help         # 查看所有命令
```

### 服务器管理
```bash
./manage.sh start     # 启动
./manage.sh stop      # 停止
./manage.sh restart   # 重启
./manage.sh status    # 状态
./manage.sh log       # 查看日志
```

### 自定义参数
```bash
./server -p 8080 -l 1 -m 3 -t 16

参数说明：
  -p  端口号（默认9006）
  -l  日志模式：0=同步 1=异步
  -m  触发模式：0=LT+LT 1=LT+ET 2=ET+LT 3=ET+ET
  -t  线程数（默认8）
  -s  数据库连接数（默认8）
  -a  并发模型：0=Proactor 1=Reactor
```

## ? 性能测试

```bash
# Webbench 压力测试
cd test_pressure/webbench-1.5
make
./webbench -c 10000 -t 10 http://localhost:9006/
```

**测试结果示例：**
- 并发：10000 clients
- 时长：10 seconds
- 成功率：100%
- QPS：~50000 requests/sec

## ? 项目结构

```
WebServer/
├── docs/                    # ? 完整文档
├── main.cpp                 # 程序入口
├── webserver.h/cpp          # 服务器核心
├── config.h/cpp             # 配置管理
├── threadpool/              # 线程池
├── http/                    # HTTP处理
├── log/                     # 日志系统
├── CGImysql/                # 数据库连接池
├── timer/                   # 定时器
├── lock/                    # 同步机制
├── root/                    # 网站根目录
├── makefile                 # 构建系统
└── manage.sh                # 管理脚本
```

## ? 技术栈

- **语言**：C++11
- **I/O多路复用**：Epoll (ET/LT)
- **并发**：线程池 + Reactor/Proactor
- **数据库**：MySQL 8.0 + 连接池
- **日志**：异步日志系统
- **协议**：HTTP/1.1

## ?? 系统要求

- Linux / WSL（Ubuntu 20.04+）
- g++ 7.0+ （支持C++11）
- MySQL 8.0+

## ? 配置数据库

默认配置（可在 `webserver.cpp` 中修改）：
```cpp
user = "root";
passwd = "";           // 改为你的MySQL密码
databasename = "xhrdb";
```

## ? 故障排查

**端口占用：**
```bash
lsof -i :9006              # 查看占用
./server -p 8080           # 换端口
```

**MySQL连接失败：**
```bash
sudo systemctl start mysql  # 启动MySQL
mysql -u root -p            # 测试连接
```

**编译错误：**
```bash
make clean && make          # 清理重编译
make deps                   # 安装依赖
```

## ? 许可证

MIT License - 详见 [LICENSE](LICENSE)

## ? Star History

如果觉得项目有帮助，欢迎 ? Star 支持！

---

**完整文档**：[docs/README.md](docs/README.md)
