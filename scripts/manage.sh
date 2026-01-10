#!/bin/bash
# -*- coding: utf-8 -*-
###############################################################################
# Web Server Management Script
# Purpose: Quick start, stop, and restart server
# Author: Your Name
# Date: 2026-01-09
###############################################################################

# Set UTF-8 encoding
export LANG=en_US.UTF-8
export LC_ALL=en_US.UTF-8

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # 无颜色

# Server configuration
SERVER_BIN="../bin/webserver"
SERVER_NAME="webserver"
DEFAULT_PORT=9006
LOG_FILE="../server.log"

# 打印信息函数
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

# Check if server is running
check_server() {
    if pgrep -f "${SERVER_NAME}" > /dev/null; then
        return 0  # 运行中
    else
        return 1  # 未运行
    fi
}

# 启动服务器
start_server() {
    if check_server; then
        print_warning "Server is already running"
        ps aux | grep "./${SERVER_NAME}" | grep -v grep
        return
    fi
    
    print_info "Starting server..."
    
    # Check if executable exists
    if [ ! -f "./${SERVER_NAME}" ]; then
        print_error "Executable ${SERVER_NAME} not found. Please compile first: make"
        return 1
    fi
    
    # Check if MySQL is running
    if ! service mysql status > /dev/null 2>&1; then
        print_warning "MySQL not running, starting..."
        sudo service mysql start
    fi
    
    # Start server in background
    nohup ./${SERVER_NAME} -p ${DEFAULT_PORT} > ${LOG_FILE} 2>&1 &
    
    sleep 1
    
    if check_server; then
        print_info "Server started successfully!"
        print_info "Access URL: http://localhost:${DEFAULT_PORT}/"
        ps aux | grep "${SERVER_NAME}" | grep -v grep
    else
        print_error "Server failed to start. Check log: tail -f ${LOG_FILE}"
        return 1
    fi
}

# 停止服务器
stop_server() {
    if ! check_server; then
        print_warning "Server is not running"
        return
    fi
    
    print_info "Stopping server..."
    pkill -f "${SERVER_NAME}"
    
    sleep 1
    
    if ! check_server; then
        print_info "Server stopped"
    else
        print_error "Failed to stop server, forcing..."
        pkill -9 -f "${SERVER_NAME}"
    fi
}

# 重启服务器
restart_server() {
    print_info "Restarting server..."
    stop_server
    sleep 1
    start_server
}

# 查看服务器状态
status_server() {
    if check_server; then
        print_info "Server is running"
        ps aux | grep "${SERVER_NAME}" | grep -v grep
        print_info "Access URL: http://localhost:${DEFAULT_PORT}/"
    else
        print_warning "Server is not running"
    fi
}

# 查看日志
show_logs() {
    if [ -f "${LOG_FILE}" ]; then
        print_info "Recent log content:"
        tail -30 ${LOG_FILE}
    else
        print_warning "Log file does not exist"
    fi
}

# 实时查看日志
follow_logs() {
    if [ -f "${LOG_FILE}" ]; then
        print_info "Following log (Press Ctrl+C to exit):"
        tail -f ${LOG_FILE}
    else
        print_error "Log file does not exist"
    fi
}

# 编译服务器
build_server() {
    print_info "Building server..."
    make clean
    make
    
    if [ $? -eq 0 ]; then
        print_info "Build successful!"
    else
        print_error "Build failed!"
        return 1
    fi
}

# 显示帮助信息
show_help() {
    echo "==========================================="
    echo "  Web服务器管理脚本"
    echo "==========================================="
    echo "用法: $0 {start|stop|restart|status|log|tail|build|help}"
    echo ""
    echo "命令说明："
    echo "  start    - 启动服务器"
    echo "  stop     - 停止服务器"
    echo "  restart  - 重启服务器"
    echo "  status   - 查看服务器状态"
    echo "  log      - 查看最近日志"
    echo "  tail     - 实时查看日志"
    echo "  build    - 编译服务器"
    echo "  help     - 显示帮助信息"
    echo ""
    echo "示例："
    echo "  $0 start              # 启动服务器"
    echo "  $0 restart            # 重启服务器"
    echo "  $0 log                # 查看日志"
    echo "==========================================="
}

# 主函数
main() {
    case "$1" in
        start)
            start_server
            ;;
        stop)
            stop_server
            ;;
        restart)
            restart_server
            ;;
        status)
            status_server
            ;;
        log)
            show_logs
            ;;
        tail)
            follow_logs
            ;;
        build)
            build_server
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            print_error "无效的命令: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
}

# 执行主函数
main "$@"
