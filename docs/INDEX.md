# 📚 WebServer 文档中心

## 文档索引

| 文档 | 说明 | 适用场景 |
|------|------|----------|
| [README.md](README.md) | 项目主文档，完整功能介绍 | 首次了解项目 |
| [QUICKSTART.md](QUICKSTART.md) | 快速入门指南 | 5分钟快速上手 |
| [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) | 项目结构说明 | 了解代码组织 |

---

## 🔧 Man Pages（手册页）

位于 `docs/manuals/` 目录下的是标准 Unix man page 格式的手册。

### 包含的手册

| 手册 | 说明 |
|------|------|
| `webserver.1` | WebServer 服务器使用手册 |
| `webbench.1` | Webbench 压测工具手册 |

### 查看方式

```bash
# 方法1：直接用 man 命令查看
man ./docs/manuals/webserver.1
man ./docs/manuals/webbench.1

# 方法2：使用 groff 转换为文本
groff -man -Tascii docs/manuals/webserver.1 | less

# 方法3：转换为 HTML 查看
groff -man -Thtml docs/manuals/webserver.1 > webserver.html

# 方法4：使用 mandoc（如果安装了）
mandoc -T ascii docs/manuals/webserver.1
```

### 安装到系统（可选）

```bash
# 复制到系统 man 目录
sudo cp docs/manuals/*.1 /usr/local/share/man/man1/

# 更新 man 数据库
sudo mandb

# 之后可以直接使用
man webserver
man webbench
```

---

## 📖 快速参考

### WebServer 常用命令

```bash
# 启动服务器
./bin/webserver -p 9006

# 高性能配置
./bin/webserver -p 9006 -l 1 -m 3 -t 16 -a 0

# 查看帮助
./bin/webserver -h
```

### WebServer 参数速查

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-p` | 端口号 | 9006 |
| `-l` | 日志模式 (0=同步, 1=异步) | 0 |
| `-m` | 触发模式 (0-3) | 0 |
| `-t` | 线程数 | 8 |
| `-s` | 数据库连接数 | 8 |
| `-a` | 并发模型 (0=Proactor, 1=Reactor) | 0 |
| `-o` | 优雅关闭 (0=关, 1=开) | 0 |
| `-c` | 关闭日志 (0=开, 1=关) | 0 |

### Webbench 压测命令

```bash
# 基本测试：1000并发，10秒
./bin/webbench -c 1000 -t 10 http://localhost:9006/

# 高压测试：10000并发
./bin/webbench -c 10000 -t 30 http://localhost:9006/

# 使用脚本（自动编译）
./scripts/run_webbench.sh http://localhost:9006/ 1000 10
```

### Webbench 参数速查

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-c` | 并发客户端数 | 1 |
| `-t` | 测试时长（秒） | 30 |
| `-f` | 不等待响应（压力模式） | 关闭 |
| `-1` | 使用 HTTP/1.0 | HTTP/1.1 |
| `-2` | 使用 HTTP/1.1 | - |
| `-p` | 通过代理测试 | 无 |

---

## 🚀 推荐阅读顺序

1. **新用户**：[QUICKSTART.md](QUICKSTART.md) → 运行起来再说
2. **想了解原理**：[README.md](README.md) → 完整技术介绍
3. **要改代码**：[PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) → 了解代码结构
4. **命令行帮助**：`man ./docs/manuals/webserver.1` → 详细参数说明
