<p align="center">
  <img src="https://img.shields.io/badge/C++-11-00599C?style=for-the-badge&logo=cplusplus&logoColor=white" alt="C++11">
  <img src="https://img.shields.io/badge/MySQL-8.0-4479A1?style=for-the-badge&logo=mysql&logoColor=white" alt="MySQL">
  <img src="https://img.shields.io/badge/Linux-Epoll-FCC624?style=for-the-badge&logo=linux&logoColor=black" alt="Linux">
  <img src="https://img.shields.io/badge/License-MIT-green?style=for-the-badge" alt="License">
</p>

<h1 align="center">🚀 WebServer</h1>

<p align="center">
  <b>高性能 C++11 Web 服务器</b><br>
  基于 Epoll + 线程池 + 连接池 + 异步日志架构，支持 10000+ 并发连接
</p>

<p align="center">
  <a href="docs/README.md">📖 完整文档</a> •
  <a href="docs/QUICKSTART.md">⚡ 快速开始</a> •
  <a href="#性能测试">📊 性能测试</a> •
  <a href="#功能特性">✨ 功能特性</a>
</p>

---

## ✨ 功能特性

| 核心功能 | 描述 |
|---------|------|
| 🔥 **高并发处理** | Epoll I/O 多路复用 + 线程池，支持 10000+ 并发 |
| 🗄️ **数据库连接池** | MySQL 连接复用，用户登录/注册系统 |
| 📝 **异步日志** | 同步/异步双模式，高性能日志记录 |
| ⏱️ **定时器管理** | 自动清理 90 秒超时的非活动连接 |
| 📤 **文件上传** | multipart/form-data 大文件上传支持 |
| 📊 **状态监控** | 实时在线用户数、连接数、访客统计 |
| 🐘 **PHP 支持** | PHP-CGI 动态页面解析 |
| 🌐 **Cloudflare 支持** | 自动识别 CF-Connecting-IP 真实 IP |

### 技术亮点

- **双并发模型**：Reactor / Proactor 模式可切换
- **触发模式**：ET / LT 边缘/水平触发可选
- **零拷贝**：mmap 内存映射加速文件传输
- **优雅关闭**：SO_LINGER 选项支持
- **内网穿透**：Cloudflare Tunnel 一键配置

---

## 🚀 快速开始

### 1️⃣ 安装依赖

```bash
# Ubuntu / Debian / WSL
sudo apt-get update
sudo apt-get install -y build-essential libmysqlclient-dev mysql-server php-cgi
```

### 2️⃣ 初始化数据库

```bash
mysql -u root -p < config/setup_db.sql
```

### 3️⃣ 编译运行

```bash
make                        # 编译
./scripts/manage.sh start   # 启动
```

### 4️⃣ 访问测试

```bash
curl http://localhost:9006/
```

🎉 **打开浏览器访问** http://localhost:9006

---

## 📁 项目结构

```
WebServer/
├── 📂 src/                    # 源代码
│   ├── core/                  #   ├── 核心模块 (main, webserver, config)
│   ├── http/                  #   ├── HTTP 请求处理
│   ├── database/              #   ├── 数据库连接池
│   ├── log/                   #   └── 异步日志系统
│   └── timer/                 #       定时器管理
│
├── 📂 include/                # 头文件
├── 📂 resources/webroot/      # 网站根目录
├── 📂 scripts/                # 管理脚本
├── 📂 config/                 # 配置文件
├── 📂 tests/benchmark/        # 压力测试工具
├── 📂 docs/                   # 文档
└── 📄 Makefile                # 构建系统
```

---

## ⚙️ 命令行参数

```bash
./bin/webserver [选项]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-p <port>` | 监听端口 | 9006 |
| `-l <0\|1>` | 日志模式：0=同步, 1=异步 | 0 |
| `-m <0-3>` | 触发模式：0=LT+LT, 3=ET+ET | 0 |
| `-t <num>` | 线程池大小 | 8 |
| `-s <num>` | 数据库连接数 | 8 |
| `-a <0\|1>` | 并发模型：0=Proactor, 1=Reactor | 0 |
| `-o <0\|1>` | 优雅关闭 | 0 |

### 配置示例

```bash
# 🔧 开发模式
./bin/webserver -p 9006 -l 0 -t 4

# 🚀 高性能模式
./bin/webserver -p 9006 -l 1 -m 3 -t 16 -a 0

# 📈 压力测试模式
./bin/webserver -p 9006 -l 1 -m 3 -c 1 -t 32
```

---

## 📊 性能测试

### 使用 Webbench

```bash
# 1000 并发，30 秒
./scripts/run_webbench.sh http://localhost:9006/ 1000 30
```

### 使用 wrk

```bash
# 安装: cd /tmp && git clone https://gitee.com/mirrors/wrk.git && cd wrk && make && sudo cp wrk /usr/local/bin/

# 测试：8 线程，1000 连接
wrk -t8 -c1000 -d30s http://localhost:9006/
```

### 测试结果

| 指标 | 数值 |
|------|------|
| **并发连接** | 10,000 |
| **测试时长** | 10 秒 |
| **成功率** | 100% |
| **QPS** | ~50,000 req/s |

---

## 🌐 内网穿透

支持 Cloudflare Tunnel 一键穿透：

```bash
# 安装 cloudflared
curl -L -o cloudflared.deb https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-amd64.deb
sudo dpkg -i cloudflared.deb

# 启动穿透（使用 HTTP/2 避免 QUIC 超时）
./scripts/tunnel_cloudflared.sh
```

服务器自动识别 Cloudflare 代理头 `CF-Connecting-IP`，正确显示访客真实 IP。

---

## 📖 文档

| 文档 | 说明 |
|------|------|
| [📚 完整文档](docs/Doc.md) | 详细技术说明 |
| [⚡ 快速开始](docs/QUICKSTART.md) | 5 分钟上手 |
| [📁 项目结构](docs/PROJECT_STRUCTURE.md) | 代码结构说明 |
| [📖 Man Pages](docs/manuals/) | Unix 手册页 |

---

## 🛠️ 技术栈

<table>
<tr>
<td align="center"><b>语言</b></td>
<td align="center"><b>I/O</b></td>
<td align="center"><b>并发</b></td>
<td align="center"><b>数据库</b></td>
<td align="center"><b>协议</b></td>
</tr>
<tr>
<td align="center">C++11</td>
<td align="center">Epoll ET/LT</td>
<td align="center">线程池</td>
<td align="center">MySQL 8.0</td>
<td align="center">HTTP/1.1</td>
</tr>
</table>

---

## 🔧 常用命令

```bash
# 编译
make                  # Debug 模式
make DEBUG=0          # Release 模式
make clean            # 清理
make rebuild          # 重新构建

# 服务器管理
./scripts/manage.sh start     # 启动
./scripts/manage.sh stop      # 停止
./scripts/manage.sh restart   # 重启
./scripts/manage.sh status    # 状态
./scripts/manage.sh log       # 查看日志

# 压力测试
make test             # 需要服务器运行中
```

---

## 📄 许可证

本项目采用 [MIT License](LICENSE) 开源。

---

<p align="center">
  ⭐ 如果这个项目对你有帮助，欢迎 Star 支持！
</p>

