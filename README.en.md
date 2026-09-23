# RtcVPN

A P2P VPN solution based on WebRTC, supporting SOCKS5 proxy protocol

## Project Introduction

RtcVPN is a Peer-to-Peer (P2P) Virtual Private Network (VPN) system implemented using WebRTC technology. It provides proxy services via the SOCKS5 protocol, supports TCP and UDP traffic forwarding, and achieves secure, private network connections.

### Core Features

- **P2P Direct Connection**: Utilizes WebRTC for end-to-end direct connection, requiring no relay servers
- **SOCKS5 Protocol Support**: Complete SOCKS5 proxy implementation, compatible with mainstream applications
- **TCP/UDP Dual Protocol Support**: Simultaneously supports TCP and UDP traffic forwarding
- **Signaling Server**: Built-in signaling mechanism for P2P connection establishment
- **Cross-Platform**: Developed based on C++, supports multiple operating systems

## System Architecture

### Component Description

```
┌─────────────────────────────────────────────────────────┐
│                        Client                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │  SOCKS5    │  │  Network    │  │    P2P Client   │ │
│  │  Server    │◄─┤  RtcApp     │◄─┤   (WebRTC)      │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
                          │ P2P Connection
                          ▼
┌─────────────────────────────────────────────────────────┐
│                        Server                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐ │
│  │  HTTP/WebSocket│ │  Session   │  │  Tunnel Session│ │
│  │  Server     │  │  Manager    │  │                │ │
│  └─────────────┘  └─────────────┘  └─────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

- **Client**: SOCKS5 proxy server running on the user side
- **Server**: Provides signaling service and session management
- **P2P Client**: WebRTC peer connection implementation
- **SessionMux**: Session multiplexing management

## Build & Compile

### Environment Requirements

- CMake 3.15+
- C++17 Compiler
- OpenSSL Development Libraries
- Boost.Asio
- libwebrtc or similar WebRTC implementation

### Build Steps

```bash
# Create build directory
mkdir build && cd build

# Configure project
cmake ..

# Compile
cmake --build .
```

### Configuration Files

Client configuration file `client.ini`:

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

Server configuration file `server.ini`:

```ini
[server]
bind_port = 8080
ws_max_message_size = 262144
```

## Usage Instructions

### Start Server

```bash
./server <config_file>
```

### Start Client

```bash
./client <config_file>
```

### Configure Browser Proxy

Chrome browser can be configured with a proxy via startup parameters:

```bash
chrome --proxy-server="socks5://127.0.0.1:1080"
```

Or using environment variables:

```bash
export http_proxy="socks5://127.0.0.1:1080"
export https_proxy="socks5://127.0.0.1:1080"
```

## Core Modules

### SOCKS5 Protocol Implementation

The project fully implements the SOCKS5 protocol defined in RFC 1928:

- Authentication (Username/Password)
- CONNECT Command (TCP Proxy)
- UDP ASSOCIATE Command (UDP Proxy)
- BIND Command

### Session Management

- **SessionIdGenerator**: Generates unique session identifiers
- **SessionMux**: Multi-session multiplexing management
- **SessionProtocol**: Data frame encoding/decoding

### Network Communication

- **TcpSocket**: Asynchronous TCP socket encapsulation
- **UdpSocket**: Asynchronous UDP socket encapsulation
- **SignalingClient**: WebSocket signaling client
- **P2PClient**: WebRTC P2P connection management

## Technical Details

### Data Frame Format

```
+--------+--------+--------+--------+
|  SID (4 bytes)  |  Length (4 bytes) |
+--------+--------+--------+--------+
|              Data (variable)        |
+--------+--------+--------+--------+
```

### P2P Connection Flow

1. Client connects to signaling server
2. Exchange SDP offers/answers
3. Establish P2P connection via ICE candidates
4. Create data channel for encrypted communication

## Testing

The project includes Python test scripts:

```bash
# SOCKS5 UDP Test
python test/socket5_udp_test.py

# UDP Functionality Test
python test/udp_test.py
```

## License

This project uses the MIT License, see LICENSE file for details.

## Related Documentation

- [SOCKS5 Protocol Explanation](doc/socks5.txt)
- [UDP Forwarding Explanation](doc/udp.txt)
- [Chrome Proxy Configuration](doc/start_chrome_with_proxy.txt)