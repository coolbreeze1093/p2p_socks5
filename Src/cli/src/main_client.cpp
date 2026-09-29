// rtcsocks —— 命令行版 P2P SOCKS5 客户端（本地端 / offerer）
//
// 无 WebSocket 信令：程序生成 OFFER 打印到控制台（可选写入 --offer-file），
// 把 OFFER 交给出口端(rtcsocks-exit) 后，将返回的 ANSWER 粘贴回本程序
// （或用 --answer-file 自动读取），P2P 建立后本地开始监听 SOCKS5。
//
// SOCKS5 会话复用 Src/cli 下的独立副本（tcp_session 修复了握手解析），
// 不修改、不影响旧 client/server。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <asio.hpp>
#include <plog/Log.h>

#include "crash_dump.h"
#include "is_std_in_terminal.h"
#include "manual_p2p.h"
#include "rtc_logger.h"
#include "socks5.h"
#include "session_mux.h"

namespace
{

constexpr const char *kProgramName = "rtcsocks";
constexpr const char *kVersion = "1.0.0";

constexpr uint16_t kDefaultSocks5Port = 10801;
constexpr int kDefaultThreadCount = 4;
constexpr int kDefaultWaitSec = 300; // 等待 ANSWER 文件的默认超时
constexpr int kGatherTimeoutSec = 30;

std::atomic<bool> running{true};

void signal_handler(int)
{
    running = false;
}

struct Options
{
    uint16_t listen_port = kDefaultSocks5Port;
    std::string stun_host; // 为空则只用 host 候选（同一局域网/直连场景）
    uint16_t stun_port = 3478;
    int threads = kDefaultThreadCount;
    std::string offer_file;  // OFFER 同时写入该文件
    std::string answer_file; // 从该文件等待 ANSWER（代替 stdin 粘贴）
    int wait_sec = kDefaultWaitSec;
    rtc::LogLevel log_level = rtc::LogLevel::Info;
};

enum class ParseResult
{
    Ok,
    Handled, // 已处理 --help / --version，直接退出
    Error,
};

void printUsage(std::FILE *out)
{
    std::fprintf(out,
                 "%s %s -- P2P SOCKS5 proxy + remote desktop, local side\n"
                 "           (manual SDP exchange, no signaling server)\n"
                 "\n"
                 "Usage:\n"
                 "  %s [options]\n"
                 "\n"
                 "Flow:\n"
                 "  1. On start an OFFER is generated and printed; send it to rtcsocks-exit\n"
                 "     running on the exit machine\n"
                 "  2. Paste the ANSWER returned by the exit side back into this program\n"
                 "     (or read it automatically via --answer-file)\n"
                 "  3. Once the P2P link is up, the local SOCKS5 proxy starts listening\n"
                 "     and/or the remote desktop window opens\n"
                 "\n"
                 "Options:\n"
                 "  -l, --listen <port>      local SOCKS5 listen port (default %u)\n"
                 "  -S, --stun <host[:port]> STUN server, required on BOTH sides across NAT\n"
                 "                           (default: none, host candidates only)\n"
                 "  -t, --threads <n>        asio IO thread count (default %d)\n"
                 "      --no-proxy           do not start the local SOCKS5 proxy\n"
                 "                           (use with --rd for remote-desktop-only mode)\n"
                 "      --offer-file <path>  also write the OFFER to this file\n"
                 "      --answer-file <path> poll this file for the ANSWER (instead of stdin)\n"
                 "      --wait <sec>         timeout in seconds when waiting for the answer\n"
                 "                           file (default %d)\n"
                 "      --log-level <level>  log level: verbose|debug|info|warning|error|none\n"
                 "                           (default info)\n"
                 "  -h, --help               show this help\n"
                 "  -V, --version            show version\n"
                 "\n"
                 "Notes:\n"
                 "  - The exit side is rtcsocks-exit. The SDP is exchanged only once;\n"
                 "    if the connection drops, exchange again.\n"
                 "  - Remote desktop requires --rd on BOTH sides.\n"
                 "  - There is no password: the SDP itself is the credential, keep it private.\n"
                 "  - Press q in the terminal (or Ctrl+C) to quit.\n",
                 kProgramName, kVersion, kProgramName,
                 kDefaultSocks5Port, kDefaultThreadCount, kDefaultWaitSec);
}

void printVersion()
{
    std::fprintf(stdout, "%s %s\n", kProgramName, kVersion);
}

bool splitInlineValue(std::string &arg, std::string &value)
{
    if (arg.rfind("--", 0) == 0)
    {
        auto eq = arg.find('=');
        if (eq != std::string::npos)
        {
            value = arg.substr(eq + 1);
            arg = arg.substr(0, eq);
            return true;
        }
    }
    return false;
}

bool parseU16(const std::string &s, uint16_t &out)
{
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        return false;

    long v = std::strtol(s.c_str(), nullptr, 10);
    if (v < 1 || v > 65535)
        return false;

    out = static_cast<uint16_t>(v);
    return true;
}

bool parseThreads(const std::string &s, int &out)
{
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        return false;

    long v = std::strtol(s.c_str(), nullptr, 10);
    if (v < 1 || v > 64)
        return false;

    out = static_cast<int>(v);
    return true;
}

bool parseWaitSec(const std::string &s, int &out)
{
    if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
        return false;

    long v = std::strtol(s.c_str(), nullptr, 10);
    if (v < 1 || v > 86400)
        return false;

    out = static_cast<int>(v);
    return true;
}

// "host"、"host:port"、"stun:host:port"、"stun://host:port"，允许留空表示不用 STUN
bool parseStun(const std::string &in, std::string &host, uint16_t &port)
{
    std::string s = in;
    if (s.rfind("stun://", 0) == 0)
        s = s.substr(7);
    else if (s.rfind("stun:", 0) == 0)
        s = s.substr(5);

    if (s.empty())
        return false;

    auto colon = s.rfind(':');
    if (colon != std::string::npos)
    {
        std::string p = s.substr(colon + 1);
        if (!p.empty() && p.find_first_not_of("0123456789") == std::string::npos)
        {
            uint16_t v = 0;
            if (!parseU16(p, v))
                return false;
            host = s.substr(0, colon);
            port = v;
            return !host.empty();
        }
        // 冒号后不是纯数字（如 IPv6 地址），整体当作 host，使用默认端口
    }

    host = s;
    port = 3478;
    return true;
}

bool parseLogLevel(const std::string &s, rtc::LogLevel &rtc_level)
{
    if (s == "verbose") { rtc_level = rtc::LogLevel::Verbose; return true; }
    if (s == "debug")   { rtc_level = rtc::LogLevel::Debug;   return true; }
    if (s == "info")    { rtc_level = rtc::LogLevel::Info;    return true; }
    if (s == "warning") { rtc_level = rtc::LogLevel::Warning; return true; }
    if (s == "error")   { rtc_level = rtc::LogLevel::Error;   return true; }
    if (s == "none")    { rtc_level = rtc::LogLevel::None;    return true; }
    return false;
}

ParseResult parseArgs(int argc, char *argv[], Options &opts, std::string &error)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        std::string inline_value;
        bool has_inline = splitInlineValue(arg, inline_value);

        auto value = [&](const char *opt) -> const char *
        {
            if (has_inline)
                return inline_value.c_str();
            if (i + 1 >= argc)
            {
                error = std::string("option ") + opt + " requires a value";
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help")
        {
            printUsage(stdout);
            return ParseResult::Handled;
        }
        else if (arg == "-V" || arg == "--version")
        {
            printVersion();
            return ParseResult::Handled;
        }
        else if (arg == "-l" || arg == "--listen")
        {
            const char *v = value("--listen");
            if (!v) return ParseResult::Error;
            if (!parseU16(v, opts.listen_port))
            {
                error = std::string("invalid listen port: ") + v + " (expected 1-65535)";
                return ParseResult::Error;
            }
        }
        else if (arg == "-S" || arg == "--stun")
        {
            const char *v = value("--stun");
            if (!v) return ParseResult::Error;
            if (!parseStun(v, opts.stun_host, opts.stun_port))
            {
                error = std::string("invalid STUN address: ") + v + " (expected host[:port])";
                return ParseResult::Error;
            }
        }
        else if (arg == "-t" || arg == "--threads")
        {
            const char *v = value("--threads");
            if (!v) return ParseResult::Error;
            if (!parseThreads(v, opts.threads))
            {
                error = std::string("invalid thread count: ") + v + " (expected 1-64)";
                return ParseResult::Error;
            }
        }
        else if (arg == "--offer-file")
        {
            const char *v = value("--offer-file");
            if (!v) return ParseResult::Error;
            opts.offer_file = v;
        }
        else if (arg == "--answer-file")
        {
            const char *v = value("--answer-file");
            if (!v) return ParseResult::Error;
            opts.answer_file = v;
        }
        else if (arg == "--wait")
        {
            const char *v = value("--wait");
            if (!v) return ParseResult::Error;
            if (!parseWaitSec(v, opts.wait_sec))
            {
                error = std::string("invalid wait seconds: ") + v;
                return ParseResult::Error;
            }
        }
        else if (arg == "--log-level")
        {
            const char *v = value("--log-level");
            if (!v) return ParseResult::Error;
            if (!parseLogLevel(v, opts.log_level))
            {
                error = std::string("invalid log level: ") + v +
                        " (expected verbose|debug|info|warning|error|none)";
                return ParseResult::Error;
            }
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            error = "unknown option: " + arg;
            return ParseResult::Error;
        }
        else
        {
            error = "unexpected argument: " + arg;
            return ParseResult::Error;
        }
    }

    return ParseResult::Ok;
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

} // namespace

int main(int argc, char *argv[])
{
    Options opts;
    std::string parse_error;

    switch (parseArgs(argc, argv, opts, parse_error))
    {
    case ParseResult::Handled:
        return 0;
    case ParseResult::Error:
        std::fprintf(stderr, "Error: %s\n\n", parse_error.c_str());
        printUsage(stderr);
        return 1;
    case ParseResult::Ok:
    default:
        break;
    }

    CrashDump::InstallCrashHandler("");

    // PLOG_* 与 rtc 日志共用一条通道: rtc::InitLogger 内部初始化 plog 实例并回调
    // rtcLogCallback (写入 rtc_socks.log 并输出到控制台)
    RtcLogger::instance().init("rtc_socks.log");
    rtc::InitLogger(opts.log_level, rtcLogCallback);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    PLOG_INFO << kProgramName << " " << kVersion << " start";

    PLOG_INFO << "stun: " << (opts.stun_host.empty() ? "<none, host candidates only>"
                                                     : opts.stun_host + ":" + std::to_string(opts.stun_port));
    if (!opts.offer_file.empty())
    {
        PLOG_INFO << "offer file: " << opts.offer_file;
    }
    if (!opts.answer_file.empty())
    {
        PLOG_INFO << "answer file: " << opts.answer_file;
    }

    asio::io_context io;
    // 防止 io.run() 因为暂时没有任务而直接退出
    auto work_guard = asio::make_work_guard(io);

    std::vector<std::thread> io_threads;
    for (int i = 0; i < opts.threads; i++)
    {
        io_threads.emplace_back([&io]()
                                {
            try
            {
                io.run();
            }
            catch (const std::exception &e)
            {
                PLOG_ERROR << "io thread exception: " << e.what();
            } });
    }

    // ---------- 业务接线：P2P 数据通道 <-> 会话复用器 <-> 本地 SOCKS5 ----------
    p2psocks::SessionMux mux(1);
    SocksServer socks_server(io, mux);

    ManualP2P p2p;
    rtc::Configuration rtc_config;
    if (!opts.stun_host.empty())
        rtc_config.iceServers.push_back({opts.stun_host, opts.stun_port});
    p2p.init(rtc_config);

    p2p.onData([&mux](const uint8_t *data, size_t len)
               {
        auto result = p2psocks::unpackMessage(reinterpret_cast<const std::byte *>(data), len);
        if (result)
            mux.on_p2p_data(1, result->payload, result->len);
        else
            PLOG_ERROR << "unpackMessage failed"; });

    mux.set_send_func([&p2p](uint32_t, const uint8_t *data, size_t len)
                      {
        try
        {
            auto buf = p2psocks::packMessage(data, len);
            p2p.send(reinterpret_cast<const uint8_t *>(buf.data()), buf.size());
        }
        catch (const std::length_error &e)
        {
            PLOG_ERROR << "packMessage: " << e.what();
        } });


    p2p.onConnected([&]()
                    {
            PLOG_INFO << "P2P connected, socks5 listening on 0.0.0.0:" << opts.listen_port;
            socks_server.start(static_cast<int16_t>(opts.listen_port));
         });


    p2p.onClosed([&]()
                 {
        PLOG_INFO << "p2p closed";
        socks_server.stop();

        running = false; });

    // ---------- 第 1 步：生成 OFFER ----------
    std::string offer_b64 = p2p.createOffer(kGatherTimeoutSec);
    if (offer_b64.empty())
    {
        PLOG_ERROR << "create offer failed";
        return 1;
    }
    PrintSdp("OFFER", offer_b64);
    std::cout << "Send the OFFER above to rtcsocks-exit running on the exit machine" << std::endl;
    if (!opts.offer_file.empty())
        WriteSdpToFile(opts.offer_file, offer_b64);

    // ---------- 第 2 步：获取 ANSWER ----------
    std::string answer_b64;
    if (!opts.answer_file.empty())
    {
        PLOG_INFO << "waiting for answer file: " << opts.answer_file;
        answer_b64 = ReadSdpFromFile(opts.answer_file, opts.wait_sec);
    }
    else
    {
        answer_b64 = ReadSdpFromStdin("the ANSWER block returned by the exit side");
    }

    if (answer_b64.empty())
    {
        PLOG_ERROR << "no valid answer";
        return 1;
    }

    if (!p2p.acceptAnswer(answer_b64))
    {
        PLOG_ERROR << "accept answer failed";
        return 1;
    }

    PLOG_INFO << "waiting for p2p connected... (input q to exit)";

    input_keyboard();

    PLOG_INFO << "closing ...";

    socks_server.stop();
    p2p.close();

    work_guard.reset();

    for (auto &t : io_threads)
    {
        if (t.joinable())
            t.join();
    }

    RtcLogger::instance().shutdown();

    return 0;
}
