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

connection_pool::connection_pool()
{
	// 初始化连接池，当前连接数和空闲连接数初始化为0
	m_CurConn = 0;
	m_FreeConn = 0;
}

// 获取连接池的单例对象
connection_pool *connection_pool::GetInstance()
{
	// 使用静态局部变量实现单例模式，保证全局只有一个连接池实例
	static connection_pool connPool;
	return &connPool;
}

// 初始化数据库连接池
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
		con = mysql_init(con); // 初始化MYSQL对象

		// 检查初始化是否成功
		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1); // 初始化失败直接退出
		}

		// 连接数据库
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

// 从连接池中获取一个空闲的数据库连接
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

// 释放当前使用的数据库连接，归还到连接池中
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

// 销毁数据库连接池，关闭所有数据库连接
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
			mysql_close(con); // 关闭数据库连接
		}
		m_CurConn = 0; // 正在使用的连接数重置为 0
		m_FreeConn = 0; // 空闲连接数重置为 0
		connList.clear(); // 清空连接池
	}

	lock.unlock(); // 解锁
}

// 获取当前空闲的数据库连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

// 连接池的析构函数，销毁所有连接
connection_pool::~connection_pool()
{
	DestroyPool(); // 调用销毁连接池的函数
}

// 连接资源管理类的构造函数
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	// 从连接池中获取一个连接
	*SQL = connPool->GetConnection();
	conRAII = *SQL; // 保存获取的连接
	poolRAII = connPool; // 保存连接池的指针
}

// 连接资源管理类的析构函数
connectionRAII::~connectionRAII()
{
	// 释放连接，归还到连接池中
	poolRAII->ReleaseConnection(conRAII);
}
