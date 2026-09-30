#include "process_new_client.h"
#include <fstream>
#include <csignal>
#include <thread>
#include <chrono>
#include "rtc_logger.h"
#include "server_config.h"
#include "is_std_in_terminal.h"

std::atomic<bool> running{true};
void signal_handler(int signal)
{
    PLOG_INFO << "Received signal " << signal << ", exit";
    running = false;
}

void input_keyboard()
{
    char c;

    while (running)
    {
        if (!isStdinTerminal())
        {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        else
        {
            std::cin.get(c);
            if (c == 'q' || c == 'Q')
            {
                PLOG_INFO << "input q, exit";

                running = false;
                break;
            }
        }
    }
    PLOG_INFO << "input keyboard exit";
}

int main(int argc, char *argv[])
{
    RtcLogger::instance().init("rtc_server.log",true);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    rtc::InitLogger(rtc::LogLevel::Debug, rtcLogCallback);
    PLOG_INFO << "RTC WebRTC C++ Server Started";
    ServerConfig server_config;
    auto exec_dir = getExecutableDir();
    ServerConfig::readConfig(server_config, exec_dir.string() + "/" + "config_server.ini");

    try
    {
        asio::io_context io;
        auto work_guard = asio::make_work_guard(io);
        ProcessNewWsClient processNewWsClient(io, server_config);
        rtc::WebSocketServerConfiguration ws_config;
        ws_config.port = server_config.ws_bind_port;
        ws_config.connectionTimeout = std::chrono::milliseconds(server_config.ws_connection_timeout);
        ws_config.maxMessageSize = server_config.ws_max_message_size;
        ws_config.bindAddress = server_config.ws_bind_address;
        ws_config.enableTls = server_config.ws_enable_tls;

        rtc::WebSocketServer rtc_ws_server(ws_config);
        rtc_ws_server.onClient([&processNewWsClient](std::shared_ptr<rtc::WebSocket> ws)
                               {
                                 processNewWsClient.newClient(ws);
                                 PLOG_DEBUG << "New WebSocket client"; });

        PLOG_INFO << "RTC WebRTC C++ Server Started...\n";

        std::vector<std::thread> io_threads;

        for (int i = 0; i < server_config.asio_thread_count; i++)
        {
            io_threads.emplace_back([&io]()
                                    { io.run(); });
        }

        input_keyboard();

        rtc_ws_server.stop();

        processNewWsClient.stop();

        work_guard.reset();

        for (auto &t : io_threads)
        {
            if (t.joinable())
                t.join();
        }
    }
    catch (std::exception &e)
    {
        PLOG_ERROR << "异常: " << e.what() << "\n";
    }
    RtcLogger::instance().shutdown();

    return 0;
}