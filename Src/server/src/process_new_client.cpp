#include "process_new_client.h"
#include <plog/Log.h>
#include "tunnel_session.h"

Socks5Session::Socks5Session(asio::io_context &io,
                             rtc::Configuration &config,
                             uint32_t session_id)
    : io_(io), config_(config), session_id_(session_id),
      mux_(session_id),
      timer_(std::make_shared<Timer>(io)),
      p2p_session_controller_(std::make_shared<P2PSessionController>())
{
    PLOG_DEBUG << "Socks5Session created  " << session_id;
}

Socks5Session::~Socks5Session()
{
    PLOG_DEBUG << "~Socks5Session destroyed  " << session_id_;
}

void Socks5Session::start(std::shared_ptr<rtc::WebSocket> ws, const std::string &passWd)
{
    auto self = shared_from_this();
    mux_.set_send_func([self, this](uint32_t conn_id, const uint8_t *data, size_t len)
                       {
        try
        {
            //PLOG_DEBUG << "send data, conn_id=" << conn_id << ", size=" << len;
            std::vector<std::byte> buf = p2psocks::packMessage(data, len);
            //PLOG_DEBUG << "send data, conn_id=" << conn_id << ", size=" << len;
            
            p2p_session_controller_->send(buf.data(), buf.size());
            
        }
        catch (const std::length_error &e)
        {
            PLOG_ERROR << "sendData: " << e.what();
            return;
        } });

    // 每个连接自己的 SessionMux，注册自己的 on_syn / on_udp_syn
    mux_.set_on_syn([self, this](uint32_t stream_id,
                                 const std::string &host, uint16_t port, Protocol protocol)
                    {
        
        PLOG_DEBUG << "rev tcp syn " << host << ":" << port
                  << " (stream_id=" << stream_id << ", session=" << self->session_id_ << ")\n";
        auto rs = std::make_shared<TunnelSession>(self->io_, self->mux_, stream_id, protocol);
        self->tunnel_sessions_[stream_id] = rs;
        rs->bind_close_func([self, this](uint32_t stream_id)
        {
            PLOG_DEBUG << "tunnel_sessions_ erase TunnelSession  " << stream_id;
            self->tunnel_sessions_.erase(stream_id);
        });
        rs->start(host, port); });

    p2p_session_controller_->bindDataChannel([this](rtc::binary data)
                                             {
                          auto result = p2psocks::unpackMessage(data.data(), data.size());
                          if(result)
                          {
                              mux_.on_p2p_data(1, result->payload, result->len);
                          }
                          else
                          {
                              PLOG_ERROR << "unpackMessage failed";
                          } });
    p2p_session_controller_->onStateChange([this](P2PSessionController::State state)
                                           {
                                            switch(state)
                                            {
                                                case P2PSessionController::State::Connected:
                                                    PLOG_DEBUG << "P2PSessionController::State::Connected";
                                                    break;
                                                case P2PSessionController::State::Closed:
                                                    PLOG_DEBUG << "P2PSessionController::State::Closed";
                                                    notifyClose();
                                                    for(auto &rs : tunnel_sessions_)
                                                    {
                                                        rs.second->close();
                                                    }
                                                    break;
                                            } });
    p2p_session_controller_->onLoginSuccess([this]()
                                            {
                                            PLOG_DEBUG << "P2PSessionController::onLoginSuccess";
                                            onLoginSuccess(); });
    p2p_session_controller_->init(config_, passWd);
    p2p_session_controller_->connect(ws);
    

    timer_->bindTimerFinish([self = shared_from_this()]()
                            {
                                if(self->tunnel_sessions_.empty())
                                {
                                    if (self->close_cb_)
                                        self->close_cb_(self->session_id_);
                                    self->timer_.reset();
                                } 
                                else
                                {
                                    self->timer_->start(1000);
                                } });
}

void Socks5Session::bindCloseFunc(std::function<void(uint32_t)> cb)
{
    close_cb_ = std::move(cb);
}

uint32_t Socks5Session::id() const { return session_id_; }

void Socks5Session::stop()
{
    p2p_session_controller_->disconnect();
}

void Socks5Session::onLoginSuccess()
{
    
}

void Socks5Session::notifyClose()
{
    timer_->start(1000);
}

ProcessNewWsClient::ProcessNewWsClient(asio::io_context &io, ServerConfig server_config)
    : io_(io), server_config_(server_config)
{
    rtc::Configuration config;
    config.iceServers = {
        {server_config.stun_ip, server_config.stun_port},
    };
    config_ = config;
    PLOG_DEBUG << "ProcessNewWsClient created";
}

ProcessNewWsClient::~ProcessNewWsClient()
{
    PLOG_DEBUG << "ProcessNewWsClient destroyed";
}

void ProcessNewWsClient::newClient(std::shared_ptr<rtc::WebSocket> ws)
{
    uint32_t id = create_session_id();

    auto session = std::make_shared<Socks5Session>(io_, config_, id);

    session->bindCloseFunc([this](uint32_t session_id)
                           {
                               PLOG_DEBUG << "ProcessNewWsClient remove session::  " << session_id;
                               client_sessions_.erase(session_id);
                           });

    client_sessions_[id] = session;
    session->start(ws, server_config_.server_password);

    PLOG_DEBUG << "newClient Socks5Session started  " << id;
}

uint32_t ProcessNewWsClient::create_session_id()
{
    if (id_ > 65535)
        id_ = 0;
    return ++id_;
}

void ProcessNewWsClient::stop()
{
    for(auto &session : client_sessions_)
    {
        session.second->stop();
    }
}
