#include <csignal>
#include <asio.hpp>
#include <plog/Log.h>
#include <fstream>
#include <thread>
#include <chrono>
#include "rtc_logger.h"
#include "network_rtc_app.h"
#include "crash_dump.h"
#include "is_std_in_terminal.h"


std::atomic<bool> running{true};
void signal_handler(int signal)
{
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
    CrashDump::InstallCrashHandler("");

    auto exec_dir = getExecutableDir();
    RtcLogger::instance().init("rtc_client.log",true);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    PLOG_INFO << "RTC WebRTC C++";
    rtc::InitLogger(rtc::LogLevel::Debug, rtcLogCallback);

    uint32_t peer_conn_id = 1;
    ClientConfig config;
    ClientConfig::readClientConfig(config, exec_dir.string() + "/" + "config_client.ini");

    asio::io_context io;
    // 防止 io.run() 因为暂时没有任务而直接退出
    auto work_guard = asio::make_work_guard(io);

    std::vector<std::thread> io_threads;

    for (int i = 0; i < config.asio_thread_count; i++)
    {
        io_threads.emplace_back([&io]()
                                {
            //io.run();
            try
            {
                io.run();
            }
            catch (const std::exception &e)
            {
                PLOG_ERROR << "io thread exception: " << e.what();
            } });
    }

    NetworkRtcApp app(peer_conn_id,io);

    app.onClose([&]()
                {
                    if(running==true)
                    {
                        PLOG_INFO << "p2p socks5 closed, please input q to exit.";
                        running = false;
                    }
                    else
                    {
                        PLOG_INFO << "p2p socks5 closed, exit.";
                    }
         });

    app.start(config);

    input_keyboard();

    PLOG_INFO << "closing ...";

    // 先关闭业务
    // 如果你的 NetworkRtcApp 有 stop() / close()，这里调用
    app.stop();

    // 允许 io.run() 退出
    work_guard.reset();

    for (auto &t : io_threads)
    {
        if (t.joinable())
            t.join();
    }

    RtcLogger::instance().shutdown();

    return 0;
}