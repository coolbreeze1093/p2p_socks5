#include <plog/Log.h>
#include "tcp_socket.h"

using asio::ip::tcp;
using namespace p2psocks;

TcpSocket::TcpSocket(asio::io_context &io)
    : io_(io), strand_(asio::make_strand(io)), socket_(strand_)
{
    PLOG_DEBUG << "TcpSession created";
}

TcpSocket::TcpSocket(asio::io_context &io, tcp::socket socket)
    : io_(io), strand_(asio::make_strand(io)), socket_(strand_)
{
    // 传进来的 socket 可能绑定在别的 executor 上（比如 acceptor
    // 所在的普通 io_context executor，而不是我们这里的 strand_）。
    // 为了让后续所有异步操作的完成回调都跑在 strand_ 里，这里把
    // 底层的 native handle 转移到本对象绑定 strand_ 的 socket 上，
    // 而不是直接把整个 socket 对象搬进来。
    std::error_code ec;
    auto endpoint = socket.local_endpoint(ec);
    auto protocol = ec ? tcp::v4() : endpoint.protocol();

    auto handle = socket.release(ec);
    if (ec)
    {
        PLOG_ERROR << "release external socket failed: " << ec.message();
        return;
    }

    socket_.assign(protocol, handle, ec);
    if (ec)
    {
        PLOG_ERROR << "assign external socket failed: " << ec.message();
        return;
    }

    is_connected_ = true;
    PLOG_DEBUG << "TcpSession created from external connected socket";
}

TcpSocket::~TcpSocket()
{
    PLOG_DEBUG << "TcpSession destroyed";
    write_queue_.clear();
}

void TcpSocket::setConnectCallback(ConnectCallback cb)
{
    connect_callback_ = std::move(cb);
}

void TcpSocket::setDataCallback(DataCallback cb)
{
    data_callback_ = std::move(cb);
}

void TcpSocket::setCloseCallback(CloseCallback cb)
{
    close_callback_ = std::move(cb);
}

void p2psocks::TcpSocket::setWriteQueueCallback(WriteQueueCallback cb)
{
    write_queue_callback_ = std::move(cb);
}

void TcpSocket::connect(const std::string &host, uint16_t port)
{
    // 把实际发起连接的动作丢进 strand_ 执行，
    // 这样即使 io_context 由多个线程 run()，
    // host_/port_/socket_ 等状态也不会被并发读写。
    auto self(shared_from_this());
    asio::post(strand_, [this, self, host, port]()
               {
        host_ = host;
        port_ = port;

        auto resolver = std::make_shared<tcp::resolver>(strand_);
        resolver->async_resolve(
            host, std::to_string(port),
            [this, self, resolver](std::error_code ec, tcp::resolver::results_type results)
            {
                if (ec)
                {
                    PLOG_ERROR << "resolve host failed: " << ec.message()
                               << ", host=" << host_ << ", port=" << port_;
                    if (connect_callback_)
                    {
                        connect_callback_(false);
                    }
                    return;
                }

                asio::async_connect(
                    socket_, results,
                    [this, self](std::error_code ec, const tcp::endpoint &endpoint)
                    {
                        if (ec)
                        {
                            PLOG_ERROR << "connect target failed: " << ec.message()
                                       << ", host=" << host_ << ", port=" << port_;
                            if (connect_callback_)
                            {
                                connect_callback_(false);
                            }
                            return;
                        }

                        PLOG_DEBUG << "connect target success, target="
                                   << endpoint.address().to_string()
                                   << ", host=" << host_ << ", port=" << port_;

                        is_connected_ = true;
                        if (connect_callback_)
                        {
                            connect_callback_(true);
                        }
                        do_read();
                    });
            }); });
}

void TcpSocket::start()
{
    auto self(shared_from_this());
    asio::post(strand_, [this, self]()
               {
        if (!is_connected_)
        {
            PLOG_ERROR << "start() called but socket is not connected";
            return;
        }
        if (is_closing_ || is_closed_)
        {
            return;
        }
        do_read(); });
}

void TcpSocket::send(const uint8_t *data, size_t len)
{
    // 先在调用方线程把数据拷贝出来（避免依赖调用方保证 data
    // 指针在异步操作完成前一直有效），再把入队 + 触发发送的
    // 逻辑丢进 strand_，串行执行。
    auto self(shared_from_this());
    std::vector<uint8_t> buf(data, data + len);
    asio::post(strand_, [this, self, buf = std::move(buf)]() mutable
               {
        if (!is_connected_ || is_closing_ || is_closed_)
        {
            PLOG_ERROR << "send called before connected or after close, data dropped";
            return;
        }

        write_queue_.emplace_back(std::move(buf));
        if (!is_sending_)
        {
            is_sending_ = true;
            do_write();
        } });
}

void TcpSocket::close()
{
    auto self(shared_from_this());
    asio::post(strand_, [this, self]()
               {
                   if (is_closing_ || is_closed_)
                   {
                       return;
                   }
                   is_closing_ = true;

                   // 尚未连接成功（比如解析/连接还在进行中）时，没有数据需要发送，直接关闭
                   if (!is_connected_)
                   {
                       do_close();
                       return;
                   }

                   // 如果当前没有正在发送、且队列为空，说明没有数据要"发完"，直接关闭
                   if (!is_sending_ && write_queue_.empty())
                   {
                       do_close();
                   }
                   // 否则等 do_write() 把队列发完后，会检测到 is_closing_ 并调用 do_close()
               });
}

void p2psocks::TcpSocket::startReading()
{
    auto self(shared_from_this());

    asio::post(strand_, [this, self]()
    {
        if (is_reading_ || is_closing_ || is_closed_)
        {
            return;
        }

        is_reading_ = true;

        if (is_connected_)
        {
            do_read();
        }
    });
}

void p2psocks::TcpSocket::stopReading()
{
    auto self(shared_from_this());

    asio::post(strand_, [this, self]()
    {
        is_reading_ = false;
    });
}

void TcpSocket::do_write()
{
    auto self(shared_from_this());
    asio::async_write(
        socket_, asio::buffer(write_queue_.front()),
        [this, self](std::error_code ec, std::size_t)
        {
            if (ec)
            {
                PLOG_ERROR << "do_write error: " << ec.message();
                is_closing_ = true;
                do_close();
                return;
            }

            write_queue_.pop_front();
            if (!write_queue_.empty())
            {
                do_write();
                return;
            }

            if (write_queue_status_ == WriteQueueStatus::Danger && write_queue_.size() < 2000)
            {
                write_queue_status_ = WriteQueueStatus::Safety;
                if (write_queue_callback_)
                {
                    write_queue_callback_(write_queue_status_);
                }
            }
            else if (write_queue_status_ == WriteQueueStatus::Safety && write_queue_.size() >= 4000)
            {
                write_queue_status_ = WriteQueueStatus::Danger;
                if (write_queue_callback_)
                {
                    write_queue_callback_(write_queue_status_);
                }
            }

            is_sending_ = false;

            // 队列已经发送完毕；如果外部已经请求关闭，此时真正关闭
            if (is_closing_)
            {
                do_close();
            }
        });
}

void TcpSocket::do_read()
{
    auto self(shared_from_this());
    socket_.async_read_some(
        asio::buffer(read_buf_),
        [this, self](std::error_code ec, std::size_t n)
        {
            if (ec)
            {
                PLOG_ERROR << "do_read error: " << ec.message();
                // 对端出错/断开，不再需要保证队列发送，直接关闭
                is_closing_ = true;
                do_close();
                return;
            }

            if (data_callback_)
            {
                data_callback_(read_buf_.data(), n);
            }
            
            if (!is_reading_)
            {
                return;
            }

            do_read();
        });
}

void TcpSocket::do_close()
{
    // do_close() 只会在 strand_ 里被调用（do_write/do_read 的
    // handler 本身就绑定在 strand_ 上；close() 也是通过
    // asio::post(strand_, ...) 转进来的），strand 保证同一时刻
    // 只有一个 handler 在跑，因此这里不需要加锁。
    if (is_closed_)
    {
        return;
    }
    is_closed_ = true;

    if (socket_.is_open())
    {
        std::error_code ec;
        socket_.cancel(ec);
        if (ec)
        {
            PLOG_ERROR << "socket cancel error: " << ec.message();
        }
        socket_.close(ec);
        if (ec)
        {
            PLOG_ERROR << "socket close error: " << ec.message()
                       << ", value=" << ec.value();
        }
    }

    // 只有连接曾经成功建立过，才触发 CloseCallback；
    // 连接失败的情况由 ConnectCallback(false) 表达，避免重复通知。
    if (/* is_connected_ &&  */close_callback_)
    {
        close_callback_();
    }
}