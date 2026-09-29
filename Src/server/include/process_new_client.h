#pragma once
#include "session_mux.h"
#include "tunnel_session.h"
#include "timer.h"
#include "p2p_session_controller.h"
#include "server_config.h"

class Socks5Session : public std::enable_shared_from_this<Socks5Session>
{
public:
    Socks5Session(asio::io_context &io,
                  rtc::Configuration &config,
                  uint32_t session_id);

    ~Socks5Session();

    void start(std::shared_ptr<rtc::WebSocket> ws,const std::string &passWd);

    void bindCloseFunc(std::function<void(uint32_t)> cb);

    uint32_t id() const;

    void stop();

private:
    void onLoginSuccess();

    void notifyClose();

    asio::io_context &io_;
    rtc::Configuration &config_;
    uint32_t session_id_;

    SessionMux mux_; // 独立的 mux，每个 session 一份

    /* std::shared_ptr<ws_server> ws_;
    std::shared_ptr<p2p_server> p2p_; */

    std::shared_ptr<Timer> timer_;

    std::shared_ptr<P2PSessionController> p2p_session_controller_;

    std::unordered_map<uint32_t, std::shared_ptr<TunnelSession>> tunnel_sessions_;

    std::function<void(uint32_t)> close_cb_;
};

class ProcessNewWsClient
{
public:
    ProcessNewWsClient(asio::io_context &io, ServerConfig server_config);

    ~ProcessNewWsClient();

    void newClient(std::shared_ptr<rtc::WebSocket> ws);

    uint32_t create_session_id();

    void stop();

private:
    asio::io_context &io_;
    rtc::Configuration config_;

    std::unordered_map<uint32_t, std::shared_ptr<Socks5Session>> client_sessions_;
    uint32_t id_ = 0;
    ServerConfig server_config_;
};