#include "tunnel_session.h"

TunnelSession::TunnelSession(asio::io_context &io, SessionMux &mux,
                             uint32_t stream_id, Protocol protocol)
    : io_(io), mux_(mux), stream_id_(stream_id), protocol_(protocol)
{
    PLOG_DEBUG << "TunnelSession::TunnelSession stream_id=" << stream_id_;
    switch (protocol_)
    {
    case Protocol::HttpPlain:
        session_interface_ = std::make_shared<HttpSession>(io_);
        break;
    case Protocol::UdpAssociate:
        session_interface_ = std::make_shared<UdpClient>(io_);
        break;
    case Protocol::Socks5Connect:
    case Protocol::HttpsConnect:
        session_interface_ = std::make_shared<TcpSession>(io_);
        break;
    default:
        PLOG_ERROR << "protocol is not protocol";
        break;
    }
}

TunnelSession::~TunnelSession()
{
    PLOG_DEBUG << "TunnelSession::~ stream_id=" << stream_id_;
}

void TunnelSession::init()
{
    session_ = mux_.create_session(stream_id_);

    auto self = shared_from_this();

    session_->set_on_data([this,self](const uint8_t *d, size_t n, Protocol protocol)
                          {
        // 来自本地端(浏览器)的数据 -> 写给目标服务器
        self->p2p_revData(d, n, protocol); });
    session_->set_on_close([this,self](Protocol protocol)
                           {
                            PLOG_DEBUG << "rev p2p close stream_id=" << stream_id_;
        self->close(); 
    
    });
    session_->set_on_data_ctrl([this,self](CtrlType ctrl, Protocol protocol)
                               {
        self->p2p_revCtrl(ctrl, protocol); });

    session_interface_->bind_close_func([this,self]()
                                        { 
                                            mux_.remove_session(stream_id_);
                                            session_.reset();
                                            if(is_closed_)
                                            {
                                                return;
                                            }
                                            is_closed_ = true;
                                            if(close_func_)
                                            {
                                                close_func_(stream_id_);
                                            }
                                            //auto si = session_interface_;
                                            session_interface_.reset();
                                        });

    session_interface_->bind_send_data([this,self](const uint8_t *d, size_t n)
                                       {
        mux_.send_data(stream_id_, d, n,protocol_); });

    session_interface_->bind_send_finish([this,self]()
                                         {
        PLOG_DEBUG << "send p2p fin stream_id=" << stream_id_;
        mux_.send_fin(stream_id_,protocol_); });
    session_interface_->bind_send_sync([this,self](bool success)
                                       {
        PLOG_DEBUG << "send p2p synack stream_id=" << stream_id_ << " success=" << success;
        mux_.send_synack(stream_id_,success,protocol_); });
}

void TunnelSession::p2p_revData(const uint8_t *d, size_t n, Protocol protocol)
{
    switch(protocol)
    {
    case Protocol::HttpPlain:
        session_interface_->revP2pData(d, n);
        break;
    case Protocol::UdpAssociate:
        session_interface_->revP2pData(d, n);
        break;
    case Protocol::Socks5Connect:
    case Protocol::HttpsConnect:
        session_interface_->revP2pData(d, n);
        break;
    case Protocol::Unknown:
    default:
        PLOG_ERROR << "protocol is not protocol";
        break;
    }
}

void TunnelSession::p2p_revCtrl(CtrlType ctrl, Protocol protocol)
{
    switch (ctrl)
    {
    case CtrlType::receive:
        session_interface_->start_receive();
        break;
    case CtrlType::pause:
        session_interface_->pause_receive();
        break;
    }
}