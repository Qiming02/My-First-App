# 数据备份应用

这是一个用C++编写的跨平台数据备份工具，支持完整备份和增量备份。

## 功能特点
- 完整备份
- 增量备份（基于MD5校验）
- 备份历史记录
- 跨平台支持（Windows/Linux/macOS）

## 编译说明

### 依赖项
- C++17 编译器
- OpenSSL 库

### Linux/macOS 编译
```bash
g++ -std=c++17 -o backup_app backup_app.cpp -lssl -lcrypto











.cpp文件为本项目的源码
