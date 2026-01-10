-- 创建数据库和用户表
CREATE DATABASE IF NOT EXISTS xhrdb;

USE xhrdb;

-- 创建用户表
CREATE TABLE IF NOT EXISTS user (
    username CHAR(50) NOT NULL,
    passwd CHAR(50) NOT NULL,
    PRIMARY KEY(username)
);

-- 插入测试用户 (可选)
INSERT INTO user VALUES('testuser', 'testpass123') ON DUPLICATE KEY UPDATE passwd='testpass123';

-- 显示表结构
DESCRIBE user;

-- 显示已有数据
SELECT * FROM user;
