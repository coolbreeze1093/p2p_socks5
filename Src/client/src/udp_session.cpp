#include "udp_session.h"
#include <plog/Log.h>
#include <asio.hpp>
#include <session_protocol.h>

using asio::ip::udp;
using SessionMux = p2psocks::SessionMux;
using Session = p2psocks::Session;

UdpSession::UdpSession(asio::io_context &io_context, SessionMux &mux, uint32_t session_id)
    : io_(io_context), mux_(mux), socket_(std::make_shared<UdpSocket>(io_context)), session_id_(session_id)
{
    PLOG_DEBUG << "UdpSession created, session_id: " << session_id_;
    
}

UdpSession::~UdpSession()
{
    PLOG_DEBUG << "UdpSession destroyed, session_id: " << session_id_;
}

void UdpSession::close()
{
    PLOG_DEBUG << "UdpSession close, session_id: " << session_id_;
    if (socket_)
    {
        socket_->close();
    }
}

void UdpSession::start(std::function<void(bool success, int port)> on_ready)
{
    auto self = shared_from_this();
    socket_->setCloseCallback([this,self]()
                              { PLOG_DEBUG << "UdpSession close session_id:" << session_id_;
                                socket_.reset();
                              });
    socket_->setDataCallback([this,self](const std::string &remote_host, uint16_t remote_port, const uint8_t *d, size_t n)
                             {
        if (!client_known_)
            {
                remote_ip_ = remote_host;
                remote_port_ = remote_port;
                client_known_ = true;
                PLOG_INFO << "first packet, record client endpoint stream_id:" << session_id_ << " " << remote_ip_ << ":" << remote_port_;
            }
            else if (remote_host != remote_ip_)
            {
                PLOG_WARNING << "source ip changed, reject packet stream_id:" << session_id_;
                remote_ip_ = remote_host;
                return;
            }
            else if (remote_port != remote_port_)
            {
                PLOG_INFO << "port changed, update client endpoint stream_id:" << session_id_;
                remote_port_ = remote_port;
            }
        send_p2p_data(d, n); });
    socket_->setWriteQueueCallback([this,self](p2psocks::WriteQueueStatus queue)
                                   {
                                    p2psocks::CtrlType type = p2psocks::CtrlType::pause;
                                    if(queue == p2psocks::WriteQueueStatus::Danger)
                                    {
                                        type = p2psocks::CtrlType::pause;
                                    }
                                    else
                                    {
                                        type = p2psocks::CtrlType::receive;
                                    }
                                    mux_.send_data_ctrl(session_id_, type, protocol_); });
    // open+bind 完成后（在 strand 内）才回调，此时取端口是安全的；
    // 原实现在 start() 返回后立刻 getLocalPort()，与异步打开存在竞态，
    // 会抛异常且被 libdatachannel 回调静默吞掉，导致 UDP ASSOCIATE 无应答
    socket_->setOpenCallback([this, self, on_ready](bool success)
                             {
                                 int port = -1;
                                 if (success)
                                 {
                                     PLOG_DEBUG << "UDP server listening on port "
                                                << socket_->getLocalPort()
                                                << ", session_id: " << session_id_;
                                     port = socket_->getLocalPort();
                                 }
                                 on_ready(success, port); });
    socket_->start();
}

void UdpSession::revP2pData(const uint8_t *d, size_t n)
{
    std::string host;
    uint16_t port;
    std::shared_ptr<std::vector<uint8_t>> data = nullptr;
    if (!p2psocks::decode_udp_payload(d, n, host, port, data))
    {
        PLOG_ERROR << "decode_udp_payload failed, session_id: " << session_id_;
        return;
    }

    auto reply = std::make_shared<std::vector<uint8_t>>();

    reply->push_back(0x00);
    reply->push_back(0x00);
    reply->push_back(0x00);
    reply->push_back(0x01);

    asio::ip::address_v4 addr =
        asio::ip::make_address_v4(host);

    auto bytes = addr.to_bytes();

    reply->insert(reply->end(),
                  bytes.begin(),
                  bytes.end());

    reply->push_back((port >> 8) & 0xff);
    reply->push_back(port & 0xff);

    reply->insert(reply->end(), data->begin(), data->end());

    socket_->send(reply,remote_ip_,remote_port_);
}

int UdpSession::getLocalPort()
{
    return socket_->getLocalPort();
}

std::string UdpSession::get_local_ip()
{
    return socket_->get_local_ip();
}

void UdpSession::p2p_data_ctrl(p2psocks::CtrlType ctrl)
{
    switch (ctrl)
    {
    case p2psocks::CtrlType::pause:
        socket_->stopReading();
        break;
    case p2psocks::CtrlType::receive:
        socket_->startReading();
        break;
    default:
        break;
    }
}

void UdpSession::send_p2p_data(const uint8_t *d, size_t n)
{
    if (d[3] == 0x01)
    {
        send_ipv4(d, n);
    }
    else if (d[3] == 0x03)
    {
        send_domain(d, n);
    }
    else
    {
        PLOG_ERROR << "invalid network type stream_id:" << session_id_;
    }
}

void UdpSession::send_ipv4(const uint8_t *d, size_t n)
{
    if (n < 10)
    {
        PLOG_WARNING << "invalid ipv4 packet stream_id:" << session_id_;
        return;
    }
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), "%d.%d.%d.%d", d[4], d[5],
                  d[6], d[7]);
    std::string target_host = tmp;
    int target_port = (uint16_t(d[8]) << 8) | d[9];

    auto payload = p2psocks::encode_udp_payload(target_host, target_port, d + 10, n - 10);

    mux_.send_data(session_id_, payload.data(), payload.size(), protocol_);
}

void UdpSession::send_domain(const uint8_t *d, size_t n)
{
    if (n < 5)
    {
        PLOG_WARNING << "invalid domain packet stream_id:" << session_id_;
        return;
    }
    int len = d[4];
    if (5 + len + 2 > (int)n)
    {
        PLOG_WARNING << "invalid domain packet 2 stream_id:" << session_id_;
        return;
    }

    std::string target_host(d + 5, d + 5 + len);
    int target_port = (uint16_t(d[6 + len]) << 8) | d[7 + len];

    auto payload = p2psocks::encode_udp_payload(target_host, target_port, d + 8 + len, n - 8 - len);
    mux_.send_data(session_id_, payload.data(), payload.size(), protocol_);
}