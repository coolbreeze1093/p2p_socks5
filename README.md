# RtcVPN

基于 WebRTC 的 P2P VPN 解决方案，支持 SOCKS5 代理协议

## 项目简介

RtcVPN 是一个利用 WebRTC 技术实现的点对点（P2P）虚拟专用网络（VPN）系统。它通过 SOCKS5 协议提供代理服务，支持 TCP 和 UDP 流量转发，实现安全、私密的网络连接。

### 核心特性

- **P2P 直接连接**：利用 WebRTC 实现端到端直连，无需中转服务器
- **SOCKS5 协议支持**：完整的 SOCKS5 代理实现，兼容主流应用
- **TCP/UDP 双协议支持**：同时支持 TCP 和 UDP 流量转发
- **信号服务器**：内置信令机制用于 P2P 连接建立
- **跨平台**：基于 C++ 开发，支持多种操作系统

## 系统架构

### 组件说明

```
┌─────────────────────────────────────────────────────────┐
│                        客户端                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │  SOCKS5    │  │  Network    │  │    P2P Client   │ │
│  │  Server    │◄─┤  RtcApp     │◄─┤   (WebRTC)      │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
                          │ P2P 连接
                          ▼
┌─────────────────────────────────────────────────────────┐
│                        服务端                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │  HTTP/WebSocket│ │  Session   │  │  Tunnel Session│ │
│  │  Server     │  │  Manager    │  │                │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

- **Client**: 运行在用户端的 SOCKS5 代理服务器
- **Server**: 提供信令服务和会话管理
- **P2P Client**: WebRTC 对等连接实现
- **SessionMux**: 会话多路复用管理

## 编译构建

### 环境要求

- CMake 3.15+
- C++17 编译器
- OpenSSL 开发库
- Boost.Asio
- libwebrtc 或类似的 WebRTC 实现

### 构建步骤

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
cmake --build .
```

### 配置文件

客户端配置文件 `client.ini`:

```ini
[network]
stun_port = 3478
ws_max_message_size = 262144
ws_connection_timeout = 10
ws_enable_tls = false
ws_disable_tls_verification = false
ws_max_outstanding_pings = 5
ws_ping_interval = 5

[proxy]
socks5_bind_port = 1080

[thread]
asio_thread_count = 2
```

服务端配置文件 `server.ini`:

```ini
[server]
bind_port = 8080
ws_max_message_size = 262144
```

## 使用说明

### 服务端启动

```bash
./server <config_file>
```

### 客户端启动

```bash
./client <config_file>
```

### 配置浏览器代理

Chrome 浏览器可通过启动参数配置代理：

```bash
chrome --proxy-server="socks5://127.0.0.1:1080"
```

或使用环境变量：

```bash
export http_proxy="socks5://127.0.0.1:1080"
export https_proxy="socks5://127.0.0.1:1080"
```

## 核心模块

### SOCKS5 协议实现

项目完整实现了 RFC 1928 定义的 SOCKS5 协议：

- 身份验证（Username/Password）
- CONNECT 命令（TCP 代理）
- UDP ASSOCIATE 命令（UDP 代理）
- BIND 命令

### 会话管理

- **SessionIdGenerator**: 生成唯一会话标识
- **SessionMux**: 多会话多路复用管理
- **SessionProtocol**: 数据帧编码/解码

### 网络通信

- **TcpSocket**: 异步 TCP 套接字封装
- **UdpSocket**: 异步 UDP 套接字封装
- **SignalingClient**: WebSocket 信令客户端
- **P2PClient**: WebRTC P2P 连接管理

## 技术细节

### 数据帧格式

```
+--------+--------+--------+--------+
|  SID (4 bytes)  |  Length (4 bytes) |
+--------+--------+--------+--------+
|              Data (variable)        |
+--------+--------+--------+--------+
```

### P2P 连接流程

1. 客户端连接信号服务器
2. 交换 SDP offers/answers
3. 通过 ICE candidate 建立 P2P 连接
4. 创建数据通道进行加密通信

## 测试

项目中包含 Python 测试脚本：

```bash
# SOCKS5 UDP 测试
python test/socket5_udp_test.py

# UDP 功能测试
python test/udp_test.py
```

## 许可证

本项目采用 MIT 许可证，详见 LICENSE 文件。

## 相关文档

- [SOCKS5 协议详解](doc/socks5.txt)
- [UDP 转发说明](doc/udp.txt)
- [Chrome 代理配置](doc/start_chrome_with_proxy.txt)