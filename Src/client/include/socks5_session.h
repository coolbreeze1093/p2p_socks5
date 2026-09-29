#pragma once
// 2026-09 修复 SOCKS5 握手解析缺陷（详见 socks5_session.cpp 顶部说明）：
//   - consume_pending 改为滚动偏移消费，不再从"块首绝对偏移"读字段
//   - 新增 socks_pending_ 跨包缓冲：一个握手步骤分多个 TCP 段到达也能正确解析
//   - 新增 nmethods_ 记录问候包中的认证方法数
#include <asio.hpp>
#include <deque>
#include <iostream>
#include <memory>
#include "session_mux.h"
#include "udp_session.h"
#include "http_parse.h"
#include "tcp_socket.h"

class Socks5Session : public std::enable_shared_from_this<Socks5Session>
{
    using tcp = asio::ip::tcp;
    using Protocol = p2psocks::Protocol;
    using Session = p2psocks::Session;
    using SessionMux = p2psocks::SessionMux;
public:
    Socks5Session(asio::io_context &io, tcp::socket socket, SessionMux &mux, uint32_t session_id);

    ~Socks5Session();

    void start();

    void set_on_close(std::function<void(uint32_t)> on_close);

    void close();

private:
    void process_data(const uint8_t *data, size_t len);
    // 返回本次消费掉的字节数（数据不足返回 0，等待下一包）
    size_t consume_pending(const uint8_t *data, size_t len);
    size_t handle_greeting_version(const uint8_t *data, size_t len);
    size_t handle_socks_methods(const uint8_t *data, size_t len);
    size_t handle_socks_tcp_udp(const uint8_t *data, size_t len);
    size_t handle_socks_ipv4(const uint8_t *data, size_t len);
    size_t handle_socks_domain(const uint8_t *data, size_t len);

    void http_connected(bool ok);

    void https_connected(bool ok);

    void udp_connected(bool ok);
    // -------- 通过 P2P 隧道请求远端建立到目标的连接 --------
    void request_remote_connect();

    void p2p_data(const uint8_t *d, size_t n, Protocol protocol);

    void p2p_synack(bool ok, Protocol protocol);

    void p2p_close(Protocol protocol);

    void p2p_data_ctrl(p2psocks::CtrlType ctrl, Protocol protocol);

    void send_socks_reply(uint8_t rep_code);

    void send_udp_reply(const std::string &host, int port);

    void close_session();

    std::shared_ptr<p2psocks::TcpSocket> tcp_socket_ = nullptr;

    SessionMux &mux_;

    std::string target_host_;
    uint16_t target_port_ = 0;

    std::shared_ptr<Session> session_ = nullptr;
    std::shared_ptr<UdpSession> udp_session_ = nullptr;

    asio::io_context &io_;

    std::function<void(uint32_t)> on_close_;

    uint32_t session_id_ = 0;
    std::mutex mutex_;

    p2psocks::HttpParser http_response_parser_;
    p2psocks::HttpParser http_request_parser_;

    std::vector<std::vector<uint8_t>> pending_buf_;

    p2psocks::Phase phase_ = p2psocks::Phase::WaitGreetingVersion;
    p2psocks::Protocol protocol_ = p2psocks::Protocol::Unknown;

    bool is_closed_ = false;

    bool is_p2p_closed_ = false;

    // ---- SOCKS5 握手解析修复新增 ----
    uint8_t nmethods_ = 0;                // 问候包中的认证方法数
    std::vector<uint8_t> socks_pending_;  // 握手阶段未消费完的字节（跨包缓冲）
};