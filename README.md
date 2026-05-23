# Course Management System 课程管理系统

## Project Overview 项目简介
This is a C++ course management system developed as a university group course project.

本项目是一个基于 C++ 的课程管理系统，采用客户端/服务端架构，使用 Socket 进行网络通信，使用 JSON 作为数据交换格式，并使用 SQLite 进行数据存储。

## Tech Stack 技术栈
- C++
- Windows Socket / Winsock
- JSON
- SQLite
- Visual Studio
- AES-256-GCM communication helper

## Features 功能
- User login 用户登录
- Course information management 课程信息管理
- Course schedule management 课程时间安排管理
- Course search by code / instructor / category 支持按课程代码、教师、类别搜索
- Client-server communication 客户端与服务端通信
- SQLite database storage 使用 SQLite 数据库存储数据
- JSON request and response handling 使用 JSON 处理请求与响应

## My Contribution 我的贡献
I was mainly responsible for:

- Client and server communication logic
- JSON request and response handling
- SQLite database design and operations
- Course and schedule management functions
- Testing and debugging

我主要负责：

- 客户端与服务端通信逻辑
- JSON 请求与响应处理
- SQLite 数据库设计与操作
- 课程与课程时间安排管理功能
- 测试与调试

## Project Structure 项目结构
```text
Client.cpp       GUI client program 客户端程序
Server.cpp       Server program 服务端程序
CryptoUtils.h    AES-256-GCM communication helper 加密通信辅助模块
json.hpp         JSON library
sqlite3.c        SQLite source file
sqlite3.h        SQLite header file
ReadMe.txt       Original build notes 原始编译说明
.gitignore       Git ignore rules
```

## Build Notes 编译说明
Both Visual Studio projects use:

```text
C++ standard: ISO C++17
C standard: C11
```

Suggested Visual Studio solution structure:

```text
Project 1: Client
Source file:
- Client.cpp

Header files:
- CryptoUtils.h
- json.hpp

Project 2: Server
Source file:
- Server.cpp
- sqlite3.c

Header files:
- CryptoUtils.h
- json.hpp
- sqlite3.h
```

## How to Run 运行方式
1. Build the server project in Visual Studio.
2. Build the client project in Visual Studio.
3. Run the server program first.
4. Run the client program.
5. Click connect and log in to use the system.

运行步骤：

1. 在 Visual Studio 中编译服务端项目。
2. 在 Visual Studio 中编译客户端项目。
3. 先运行服务端程序。
4. 再运行客户端程序。
5. 在客户端中连接服务器并登录使用系统。

## Notes 说明
This project was completed as a university group assignment.

本项目为大学小组课程作业，用于学习和展示 C++ 网络通信、JSON 数据交换和 SQLite 数据库操作。
