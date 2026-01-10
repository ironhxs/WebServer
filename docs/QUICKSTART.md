# 快速使用指南

## ? 立即开始

### 一键管理（推荐）
```bash
# 赋予脚本执行权限（首次使用）
chmod +x manage.sh

# 启动服务器
./manage.sh start

# 停止服务器
./manage.sh stop

# 重启服务器
./manage.sh restart

# 查看状态
./manage.sh status

# 查看日志
./manage.sh log

# 实时日志
./manage.sh tail

# 重新编译
./manage.sh build
```

### 手动管理
```bash
# 编译
make clean
make

# 启动（前台运行）
./server -p 9006

# 启动（后台运行）
nohup ./server -p 9006 > server.log 2>&1 &

# 查看进程
ps aux | grep server

# 停止服务器
pkill -f './server'

# 查看日志
tail -f server.log
```

## ? 访问网站

启动后在浏览器访问：
- **主页**：http://localhost:9006/
- **PHP 测试**：http://localhost:9006/index.php
- **登录页面**：http://localhost:9006/judge.html
- **注册页面**：http://localhost:9006/register.html

测试账号：
- 用户名：`testuser`
- 密码：`testpass123`

## ?? 命令行参数

```bash
./server [选项]

选项：
  -p <port>       端口号（默认：9006）
  -l <0|1>        日志模式（0=同步，1=异步）
  -m <0|1|2|3>    触发模式（0=LT+LT, 1=LT+ET, 2=ET+LT, 3=ET+ET）
  -o <0|1>        优雅关闭（0=不使用，1=使用）
  -s <num>        数据库连接池大小（默认：8）
  -t <num>        线程池大小（默认：8）
  -c <0|1>        关闭日志（0=开启，1=关闭）
  -a <0|1>        并发模型（0=proactor，1=reactor）
```

### 配置示例

```bash
# 开发环境（低并发，开启日志）
./server -p 9006 -l 0 -m 0 -t 4

# 生产环境（高并发，异步日志，ET模式）
./server -p 80 -l 1 -m 3 -t 32 -s 32

# 压力测试（关闭日志，最高性能）
./server -p 9006 -l 1 -m 3 -c 1 -t 16
```

## ? 常见问题

### 1. 端口被占用
```bash
# 查看端口占用
sudo lsof -i :9006

# 杀死进程
kill -9 <PID>

# 或使用脚本停止
./manage.sh stop
```

### 2. MySQL 连接失败
```bash
# 启动 MySQL
sudo service mysql start

# 检查数据库是否存在
sudo mysql -e "SHOW DATABASES;"

# 重新创建数据库
sudo mysql < setup_db.sql
```

### 3. 编译错误
```bash
# 安装依赖
sudo apt-get install -y libmysqlclient-dev

# 清理后重新编译
make clean
make
```

### 4. 无法访问网站
```bash
# 检查服务器是否运行
./manage.sh status

# 检查防火墙
sudo ufw allow 9006

# 查看错误日志
tail -f server.log
```

## ? 项目文件说明

### 核心文件
- `main.cpp` - 主程序入口，包含详细注释
- `config.h/cpp` - 配置解析，支持命令行参数
- `webserver.h/cpp` - 服务器核心逻辑
- `manage.sh` - 服务器管理脚本（一键启动/停止）

### 功能模块
- `http/` - HTTP 请求处理
- `threadpool/` - 线程池实现
- `CGImysql/` - 数据库连接池
- `log/` - 异步日志系统
- `timer/` - 定时器管理
- `lock/` - 线程同步工具

### 网站资源
- `root/` - 网站根目录
- `root/html/` - 静态网页资源
- `root/*.html` - 登录、注册等功能页面

## ? 自定义网站

### 修改主页
编辑 `root/html/index.html`：
```html
<title>你的网站名称</title>
<h1>欢迎访问</h1>
```

### 修改样式
编辑 `root/html/style.css`：
```css
body {
    background: #f5f5f5;
}
```

### 添加新页面
```bash
# 在 root/ 目录创建新文件
nano root/my-page.html

# 访问：http://localhost:9006/my-page.html
```

## ? 性能测试

```bash
# 使用 Webbench
cd test_pressure/webbench-1.5
make
./webbench -c 1000 -t 30 http://localhost:9006/

# 使用 ab（Apache Bench）
ab -n 10000 -c 100 http://localhost:9006/
```

## ? 安全建议

1. **修改数据库密码**
   ```sql
   ALTER USER 'root'@'localhost' IDENTIFIED BY 'your_password';
   ```

2. **更改默认端口**
   ```bash
   ./server -p 8888  # 使用非标准端口
   ```

3. **配置防火墙**
   ```bash
   sudo ufw allow 9006
   sudo ufw enable
   ```

## ? 日志说明

日志文件位置：
- `server.log` - 服务器运行日志（使用 nohup 时）
- `ServerLog` - 日志系统输出（默认位置）

查看日志：
```bash
# 查看最近30行
./manage.sh log

# 实时查看
./manage.sh tail

# 或直接使用 tail
tail -f server.log
```

## ? 获取帮助

```bash
# 查看脚本帮助
./manage.sh help

# 查看命令行参数
./server -h
```

## ? 联系方式

- **问题反馈**：[GitHub Issues](your-repo-url/issues)
- **邮箱**：your.email@example.com

---

**提示**：所有核心文件都已添加详细注释，方便理解和修改！
