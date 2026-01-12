/**
 * @file database_pool.h
 * @brief MySQL连接池定义
 * @details 提供数据库连接池及RAII封装，减少频繁建连开销。
 */

#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string.h>
#include <iostream>
#include <string>
#include "thread_sync.h"
#include "log.h"

using namespace std;

/**
 * @class connection_pool
 * @brief 数据库连接池（单例）
 */
class connection_pool
{
public:
	/**
	 * @brief 获取一个可用连接
	 * @return MYSQL连接指针，失败返回NULL
	 */
	MYSQL *GetConnection();
	/**
	 * @brief 释放连接并归还到连接池
	 * @param conn 连接指针
	 * @return 是否释放成功
	 */
	bool ReleaseConnection(MYSQL *conn);
	/**
	 * @brief 获取空闲连接数量
	 * @return 空闲连接数
	 */
	int GetFreeConn();
	/**
	 * @brief 销毁连接池并释放所有连接
	 */
	void DestroyPool();

	//单例模式
	/**
	 * @brief 获取连接池单例
	 * @return 连接池实例指针
	 */
	static connection_pool *GetInstance();

	/**
	 * @brief 初始化连接池
	 * @param url 主机地址
	 * @param User 用户名
	 * @param PassWord 密码
	 * @param DataBaseName 数据库名
	 * @param Port 端口号
	 * @param MaxConn 连接池最大连接数
	 * @param close_log 日志开关
	 */
	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 

private:
	/**
	 * @brief 构造函数（私有，单例）
	 */
	connection_pool();
	/**
	 * @brief 析构函数
	 */
	~connection_pool();

	int m_MaxConn;  ///< 最大连接数
	int m_CurConn;  ///< 当前已使用连接数
	int m_FreeConn; ///< 当前空闲连接数
	locker lock;    ///< 连接池互斥锁
	list<MYSQL *> connList; ///< 连接列表
	sem reserve;    ///< 空闲连接计数信号量

public:
	string m_url;         ///< 主机地址
	string m_Port;        ///< 数据库端口号（字符串形式）
	string m_User;        ///< 数据库用户名
	string m_PassWord;    ///< 数据库密码
	string m_DatabaseName;///< 数据库名
	int m_close_log;      ///< 日志开关
};

/**
 * @class connectionRAII
 * @brief 连接资源RAII封装（自动获取与释放）
 */
class connectionRAII{

public:
	/**
	 * @brief 构造函数 - 获取连接
	 * @param con 输出连接指针
	 * @param connPool 连接池对象
	 */
	connectionRAII(MYSQL **con, connection_pool *connPool);
	/**
	 * @brief 析构函数 - 释放连接
	 */
	~connectionRAII();
	
private:
	MYSQL *conRAII;             ///< 当前管理的连接
	connection_pool *poolRAII;  ///< 连接池指针
};

#endif
