#include <asio.hpp>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include "session_mux.h"
#include "udp_socket.h"
class UdpSession : public std::enable_shared_from_this<UdpSession>
{
    using SessionMux = p2psocks::SessionMux;
    using Session = p2psocks::Session;
    using UdpSocket = p2psocks::UdpSocket;
public:
    UdpSession(asio::io_context &io_context, SessionMux &mux, uint32_t session_id);

    ~UdpSession();

    void close();

    // 异步打开 UDP 套接字；open+bind 完成后回调 on_ready(success, 绑定端口)。
    // 不能在 start() 返回后立刻取端口，套接字此时可能尚未打开（原实现的竞态）。
    void start(std::function<void(bool success, int port)> on_ready);

    void revP2pData(const uint8_t *d, size_t n);

    int getLocalPort();

    std::string get_local_ip();
    void p2p_data_ctrl(p2psocks::CtrlType ctrl);

private:
    void send_ipv4(const uint8_t *d, size_t n);

    void send_domain(const uint8_t *d, size_t n);

    void send_p2p_data(const uint8_t *d, size_t n);

    asio::io_context &io_;
    SessionMux &mux_;
    std::shared_ptr<UdpSocket> socket_;
    bool client_known_ = false;
    std::string remote_ip_;
    uint16_t remote_port_;
    uint32_t session_id_;
    p2psocks::Protocol protocol_ = p2psocks::Protocol::UdpAssociate;
};