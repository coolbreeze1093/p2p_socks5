#include "manual_p2p.h"
#include "rd_protocol.h"
#include <plog/Log.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <future>
#include <sstream>
#include <stdexcept>
#include <thread>

void ManualP2P::init(const rtc::Configuration &config)
{
    config_ = config;
}

std::string ManualP2P::createOffer(int timeout_sec, bool with_rd)
{
    try
    {
        pc_ = std::make_shared<rtc::PeerConnection>(config_);
        bindPeerConnection();

        dc_ = pc_->createDataChannel("data");
        bindDataChannel(dc_, false);

        // 远程桌面走独立通道，与代理业务互不影响
        if (with_rd)
        {
            rd_dc_ = pc_->createDataChannel(rd::kChannelLabel);
            bindDataChannel(rd_dc_, true);
        }
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR << "createOffer: " << e.what();
        return {};
    }

    return waitLocalSdp(timeout_sec);
}

bool ManualP2P::acceptAnswer(const std::string &answer_b64)
{
    try
    {
        std::string sdp = base64Decode(answer_b64);
        pc_->setRemoteDescription(rtc::Description(sdp, "answer"));
        PLOG_INFO << "remote answer accepted";
        return true;
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR << "acceptAnswer: " << e.what();
        return false;
    }
}

std::string ManualP2P::acceptOffer(const std::string &offer_b64, int timeout_sec)
{
    try
    {
        pc_ = std::make_shared<rtc::PeerConnection>(config_);
        bindPeerConnection();

        // 对端创建的 DataChannel 通过该回调接入，按 label 分发
        pc_->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc)
                           {
                               const std::string label = dc->label();
                               PLOG_INFO << "DataChannel received: " << label;
                               if (label == rd::kChannelLabel)
                               {
                                   rd_dc_ = dc;
                                   bindDataChannel(rd_dc_, true);
                               }
                               else
                               {
                                   dc_ = dc;
                                   bindDataChannel(dc_, false);
                               } });

        std::string sdp = base64Decode(offer_b64);
        pc_->setRemoteDescription(rtc::Description(sdp, "offer"));
        PLOG_INFO << "remote offer accepted";
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR << "acceptOffer: " << e.what();
        return {};
    }

    return waitLocalSdp(timeout_sec);
}

void ManualP2P::bindPeerConnection()
{
    pc_->onStateChange([this](rtc::PeerConnection::State state)
                       {
        PLOG_INFO << "PeerConnection state: " << static_cast<int>(state);
        if (state == rtc::PeerConnection::State::Failed ||
            state == rtc::PeerConnection::State::Closed)
        {
            if (closed_callback_)
                closed_callback_();
        } });

    pc_->onLocalCandidate([](rtc::Candidate candidate)
                          { PLOG_INFO << "Local candidate: " << candidate; });
}

void ManualP2P::bindDataChannel(std::shared_ptr<rtc::DataChannel> dc, bool is_rd)
{
    dc->onOpen([this, is_rd]()
               { PLOG_INFO << "DataChannel open"
                          << (is_rd ? " (rd)" : "");
                 // 任一通道首次打开即视为可用（两条通道几乎同时打开，只通知一次）
                 if (!connected_notified_.exchange(true) && connected_callback_)
                     connected_callback_(); });

    dc->onClosed([this]()
                 { PLOG_INFO << "DataChannel closed";
                   if (closed_callback_)
                       closed_callback_(); });

    dc->onError([](std::string message)
                { PLOG_ERROR << "DataChannel error: " << message; });

    dc->onMessage(
        [this, is_rd](rtc::binary message)
        {
            auto &cb = is_rd ? rd_data_callback_ : data_callback_;
            if (cb)
                cb(reinterpret_cast<const uint8_t *>(message.data()), message.size());
            else
                PLOG_WARNING << "data channel message dropped (no callback)";
        },
        [](std::string message)
        { PLOG_WARNING << "text message ignored: " << message; });
}

std::string ManualP2P::waitLocalSdp(int timeout_sec)
{
    // 收集完成时 libdatachannel 已把全部候选写入 local description，
    // 因此取到的 SDP 可直接离线交换（非 trickle）
    auto gathered = std::make_shared<std::promise<void>>();
    auto future = gathered->get_future();

    pc_->onGatheringStateChange([gathered](rtc::PeerConnection::GatheringState state)
                                {
        PLOG_INFO << "Gathering state: " << static_cast<int>(state);
        if (state == rtc::PeerConnection::GatheringState::Complete)
        {
            try { gathered->set_value(); } catch (...) {}
        } });

    // 回调注册前可能已经收集完成（无 STUN 时几乎立即完成）
    if (pc_->gatheringState() == rtc::PeerConnection::GatheringState::Complete)
    {
        try { gathered->set_value(); } catch (...) {}
    }

    if (future.wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::ready)
    {
        PLOG_ERROR << "gathering timeout (" << timeout_sec << "s)";
        return {};
    }

    auto description = pc_->localDescription();
    if (!description)
    {
        PLOG_ERROR << "local description unavailable";
        return {};
    }

    return base64Encode(std::string(*description));
}

void ManualP2P::send(const uint8_t *data, size_t len)
{
    try
    {
        if (dc_ && dc_->isOpen())
        {
            dc_->send(reinterpret_cast<const std::byte *>(data), len);
        }
        else
        {
            PLOG_WARNING << "send dropped: data channel not open (" << len << " bytes)";
        }
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR << "send: " << e.what();
    }
}

void ManualP2P::sendRd(const uint8_t *data, size_t len)
{
    try
    {
        if (rd_dc_ && rd_dc_->isOpen())
        {
            rd_dc_->send(reinterpret_cast<const std::byte *>(data), len);
        }
        else
        {
            PLOG_WARNING << "sendRd dropped: rd channel not open (" << len << " bytes)";
        }
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR << "sendRd: " << e.what();
    }
}

void ManualP2P::close()
{
    try
    {
        if (rd_dc_)
            rd_dc_->close();
        if (dc_)
            dc_->close();
        if (pc_)
            pc_->close();
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR << "close: " << e.what();
    }
}

// ---------- base64 ----------

namespace
{
const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace

std::string ManualP2P::base64Encode(const std::string &raw)
{
    std::string out;
    out.reserve(((raw.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= raw.size())
    {
        uint32_t v = (uint32_t(uint8_t(raw[i])) << 16) |
                     (uint32_t(uint8_t(raw[i + 1])) << 8) |
                     uint32_t(uint8_t(raw[i + 2]));
        out += kBase64Alphabet[(v >> 18) & 0x3F];
        out += kBase64Alphabet[(v >> 12) & 0x3F];
        out += kBase64Alphabet[(v >> 6) & 0x3F];
        out += kBase64Alphabet[v & 0x3F];
        i += 3;
    }

    size_t remaining = raw.size() - i;
    if (remaining == 1)
    {
        uint32_t v = uint32_t(uint8_t(raw[i])) << 16;
        out += kBase64Alphabet[(v >> 18) & 0x3F];
        out += kBase64Alphabet[(v >> 12) & 0x3F];
        out += "==";
    }
    else if (remaining == 2)
    {
        uint32_t v = (uint32_t(uint8_t(raw[i])) << 16) |
                     (uint32_t(uint8_t(raw[i + 1])) << 8);
        out += kBase64Alphabet[(v >> 18) & 0x3F];
        out += kBase64Alphabet[(v >> 12) & 0x3F];
        out += kBase64Alphabet[(v >> 6) & 0x3F];
        out += "=";
    }
    return out;
}

std::string ManualP2P::base64Decode(const std::string &b64)
{
    auto alphabet_value = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };

    std::string out;
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : b64)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
            continue;
        if (c == '=')
            break;
        int v = alphabet_value(c);
        if (v < 0)
            throw std::runtime_error("base64 invalid character");
        buffer = (buffer << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(char((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

// ---------- SDP 交换辅助 ----------

std::string ExtractSdp(const std::string &text)
{
    auto looks_like_sdp = [](const std::string &decoded)
    {
        return decoded.find("v=0") != std::string::npos &&
               decoded.find("m=") != std::string::npos;
    };

    auto try_decode = [&looks_like_sdp](const std::string &candidate) -> std::string
    {
        try
        {
            std::string decoded = ManualP2P::base64Decode(candidate);
            if (looks_like_sdp(decoded))
                return candidate;
        }
        catch (...) {}
        return {};
    };

    std::vector<std::string> lines;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line))
    {
        // 去首尾空白
        auto is_space = [](unsigned char c)
        { return std::isspace(c) != 0; };
        line.erase(line.begin(), std::find_if_not(line.begin(), line.end(), is_space));
        line.erase(std::find_if_not(line.rbegin(), line.rend(), is_space).base(), line.end());

        if (line.empty())
            continue;
        if (line.rfind("-----BEGIN", 0) == 0 || line.rfind("-----END", 0) == 0)
            continue;
        lines.push_back(line);
    }

    // 1) 单行即完整 base64
    for (const auto &l : lines)
    {
        std::string ok = try_decode(l);
        if (!ok.empty())
            return ok;
    }

    // 2) base64 被换行拆开时，拼接后再解码
    if (lines.size() > 1)
    {
        std::string joined;
        for (const auto &l : lines)
            joined += l;
        std::string ok = try_decode(joined);
        if (!ok.empty())
            return ok;
    }

    return {};
}

std::string ReadSdpFromStdin(const std::string &what)
{
    std::cout << "Paste " << what << " and press Enter:" << std::endl;

    std::string buffer, line;
    while (std::getline(std::cin, line))
    {
        buffer += line;
        buffer += '\n';

        std::string sdp = ExtractSdp(buffer);
        if (!sdp.empty())
            return sdp;

        if (line.rfind("-----END", 0) == 0)
            break; // 用户已结束粘贴但内容无效
    }
    return {};
}

std::string ReadSdpFromFile(const std::string &path, int timeout_sec)
{
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeout_sec);

    while (std::chrono::steady_clock::now() < deadline)
    {
        std::ifstream file(path, std::ios::binary);
        if (file)
        {
            std::ostringstream oss;
            oss << file.rdbuf();
            std::string sdp = ExtractSdp(oss.str());
            if (!sdp.empty())
                return sdp;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return {};
}

bool WriteSdpToFile(const std::string &path, const std::string &sdp_b64)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        PLOG_ERROR << "cannot write file: " << path;
        return false;
    }
    file << sdp_b64 << std::endl;
    return file.good();
}

void PrintSdp(const char *title, const std::string &sdp_b64)
{
    std::cout << "-----BEGIN " << title << "-----" << std::endl;
    std::cout << sdp_b64 << std::endl;
    std::cout << "-----END " << title << "-----" << std::endl;
}
