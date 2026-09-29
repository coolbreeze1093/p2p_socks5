#pragma once
#include <asio.hpp>
#include <deque>
#include <iostream>
#include <memory>
#include "session_mux.h"
#include <array>
#include "session_interface.h"
#include "http_session.h"
#include "udp_session.h"
#include "tcp_session.h"

using namespace p2psocks;

class TunnelSession : public std::enable_shared_from_this<TunnelSession>
{
public:
    using CloseFunc = std::function<void(uint32_t)>;
    TunnelSession(asio::io_context &io, SessionMux &mux,
                  uint32_t stream_id, Protocol protocol);
    ~TunnelSession();

    void close(){
        PLOG_DEBUG << "TunnelSession close  " << stream_id_;
        session_interface_->stop();};

    void bind_close_func(CloseFunc close_func){
        PLOG_DEBUG << "TunnelSession bind_close_func  " << stream_id_;
        close_func_ = close_func;};

    void start(const std::string &host, uint16_t port){init();session_interface_->start(host, port);};

private:
    void init();
    
    void p2p_revData(const uint8_t *d, size_t n, Protocol protocol);

    void p2p_revCtrl(CtrlType ctrl, Protocol protocol);

private:
    asio::io_context &io_;
    SessionMux &mux_;
    uint32_t stream_id_;
    Protocol protocol_;
    std::shared_ptr<SessionInterface> session_interface_;
    std::shared_ptr<Session> session_;
    CloseFunc close_func_;
    bool is_closed_ = false;
    
};

