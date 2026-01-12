/**
 * @file database_pool.cpp
 * @brief 数据库连接池实现
 * @details 使用MySQL C API实现数据库连接池，支持多线程并发访问
 * 
 * 主要使用的MySQL C API函数:
 * - mysql_init(): 初始化MYSQL对象，分配内存
 * - mysql_real_connect(): 建立与MySQL服务器的连接
 * - mysql_close(): 关闭数据库连接，释放资源
 * - mysql_query(): 执行SQL查询（在http_connection中使用）
 * - mysql_store_result(): 获取查询结果集
 * - mysql_fetch_row(): 遍历结果集行
 * - mysql_free_result(): 释放结果集内存
 * 
 * 连接池设计:
 * - 单例模式: 保证全局唯一实例
 * - 信号量: 控制可用连接数量
 * - 互斥锁: 保护连接列表的线程安全
 * - RAII封装: 自动获取/释放连接，防止泄漏
 */

#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "database_pool.h"

using namespace std;

/**
 * @brief 连接池构造函数
 */
connection_pool::connection_pool()
{
	// 初始化连接池，当前连接数和空闲连接数初始化为0
	m_CurConn = 0;
	m_FreeConn = 0;
}

/**
 * @brief 获取连接池单例
 */
connection_pool *connection_pool::GetInstance()
{
	// 使用静态局部变量实现单例模式，保证全局只有一个连接池实例
	static connection_pool connPool;
	return &connPool;
}

/**
 * @brief 初始化数据库连接池
 * @param url 主机地址
 * @param User 用户名
 * @param PassWord 密码
 * @param DBName 数据库名
 * @param Port 端口号
 * @param MaxConn 最大连接数
 * @param close_log 日志开关
 */
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	// 初始化连接池的基本配置信息
	m_url = url;           // 数据库主机地址
	m_Port = Port;         // 数据库端口
	m_User = User;         // 数据库用户名
	m_PassWord = PassWord; // 数据库密码
	m_DatabaseName = DBName; // 数据库名称
	m_close_log = close_log; // 是否关闭日志标志

	// 循环创建指定数量（MaxConn）的数据库连接
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		/**
		 * mysql_init() - 初始化MYSQL连接句柄
		 * @param mysql: 传入NULL则自动分配新对象，否则初始化已有对象
		 * @return: 成功返回MYSQL*指针，失败返回NULL
		 * 
		 * 功能:
		 * 1. 分配或初始化一个MYSQL对象
		 * 2. 初始化连接所需的内部数据结构
		 * 3. 必须在mysql_real_connect()之前调用
		 * 
		 * 注意: 返回的对象必须用mysql_close()释放
		 */
		con = mysql_init(con);

		// 检查初始化是否成功
		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1); // 初始化失败直接退出
		}

		/**
		 * mysql_real_connect() - 建立与MySQL服务器的连接
		 * @param mysql: mysql_init()返回的连接句柄
		 * @param host: 服务器主机名或IP ("localhost"或"127.0.0.1")
		 * @param user: 数据库用户名
		 * @param passwd: 用户密码
		 * @param db: 要使用的数据库名
		 * @param port: MySQL端口号（默认3306）
		 * @param unix_socket: Unix套接字路径（NULL使用默认）
		 * @param client_flag: 连接标志位（0使用默认）
		 * @return: 成功返回MYSQL*，失败返回NULL
		 * 
		 * 常用client_flag:
		 * - CLIENT_FOUND_ROWS: UPDATE返回匹配行数而非修改行数
		 * - CLIENT_MULTI_STATEMENTS: 允许多语句执行
		 * - CLIENT_COMPRESS: 使用压缩协议
		 * 
		 * 错误处理: 失败后可用mysql_error()获取错误信息
		 */
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		// 检查数据库连接是否成功
		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1); // 连接失败直接退出
		}

		// 将连接对象加入到连接池中
		connList.push_back(con);
		++m_FreeConn; // 空闲连接数增加
	}

	// 初始化信号量，初始值为空闲连接数
	reserve = sem(m_FreeConn);

	// 设置最大连接数
	m_MaxConn = m_FreeConn;
}

/**
 * @brief 获取一个空闲连接
 * @return MYSQL连接指针
 */
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;

	// 如果连接池为空，直接返回 NULL
	if (0 == connList.size())
		return NULL;

	// 等待信号量，有可用连接时继续
	reserve.wait();
	
	lock.lock(); // 加锁保护共享资源

	// 获取连接池中的第一个连接
	con = connList.front();
	connList.pop_front(); // 从连接池中移除该连接

	--m_FreeConn; // 空闲连接数减少
	++m_CurConn;  // 正在使用的连接数增加

	lock.unlock(); // 解锁
	return con; // 返回获取的连接
}

/**
 * @brief 释放连接并归还连接池
 * @param con 连接指针
 * @return 是否释放成功
 */
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	// 如果连接为空，直接返回 false
	if (NULL == con)
		return false;

	lock.lock(); // 加锁保护共享资源

	// 将连接重新加入到连接池中
	connList.push_back(con);
	++m_FreeConn; // 空闲连接数增加
	--m_CurConn;  // 正在使用的连接数减少

	lock.unlock(); // 解锁

	// 释放一个信号量，表示有新的空闲连接可用
	reserve.post();
	return true;
}

/**
 * @brief 销毁连接池
 */
void connection_pool::DestroyPool()
{
	lock.lock(); // 加锁保护共享资源

	// 遍历连接池，关闭所有的数据库连接
	if (connList.size() > 0)
	{
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			/**
			 * mysql_close() - 关闭数据库连接并释放资源
			 * @param mysql: 要关闭的MYSQL连接句柄
			 * 
			 * 功能:
			 * 1. 关闭与MySQL服务器的网络连接
			 * 2. 释放mysql_init()分配的MYSQL对象内存
			 * 3. 释放所有关联的资源（结果集、预处理语句等）
			 * 
			 * 注意:
			 * - 关闭后指针变为无效，不应再使用
			 * - 未释放的结果集也会被自动清理
			 * - 通常在程序结束或不再需要连接时调用
			 */
			mysql_close(con);
		}
		m_CurConn = 0; // 正在使用的连接数重置为 0
		m_FreeConn = 0; // 空闲连接数重置为 0
		connList.clear(); // 清空连接池
	}

	lock.unlock(); // 解锁
}

/**
 * @brief 获取空闲连接数
 * @return 空闲连接数
 */
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

/**
 * @brief 连接池析构函数
 */
connection_pool::~connection_pool()
{
	DestroyPool(); // 调用销毁连接池的函数
}

/**
 * @brief RAII连接管理构造函数 - 自动获取数据库连接
 * @param SQL 输出连接指针（二级指针用于返回值）
 * @param connPool 连接池对象
 * 
 * RAII (Resource Acquisition Is Initialization) 模式:
 * - 资源获取即初始化：在构造函数中获取资源
 * - 资源释放即析构：在析构函数中自动释放资源
 * - 利用C++栈对象生命周期自动管理资源
 * - 即使发生异常也能保证资源正确释放
 * 
 * 使用示例:
 * @code
 * MYSQL *mysql = nullptr;
 * connectionRAII connRAII(&mysql, connection_pool::GetInstance());
 * // 使用mysql进行数据库操作...
 * // 作用域结束时自动归还连接，无需手动释放
 * @endcode
 */
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	// 从连接池中获取一个连接
	*SQL = connPool->GetConnection();
	conRAII = *SQL; // 保存获取的连接（用于析构时归还）
	poolRAII = connPool; // 保存连接池指针（用于析构时调用ReleaseConnection）
}

/**
 * @brief RAII连接管理析构函数 - 自动归还数据库连接
 * 
 * 当connectionRAII对象离开作用域时自动调用:
 * 1. 将连接归还给连接池（而非关闭连接）
 * 2. 增加连接池信号量，唤醒等待的线程
 * 3. 保证连接不会泄漏
 * 
 * 这种模式避免了:
 * - 忘记释放连接导致连接泄漏
 * - 异常导致连接未释放
 * - 手动管理连接的繁琐代码
 */
connectionRAII::~connectionRAII()
{
	// 释放连接，归还到连接池中
	poolRAII->ReleaseConnection(conRAII);
}
