#pragma once

#include <rtc/rtc.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 只负责 PeerConnection / DataChannel / MediaTrack，
// 不知道信令是怎么传输的
class P2PClient : public std::enable_shared_from_this<P2PClient>
{
public:
    using BinaryMessageCallback =
        std::function<void(rtc::binary)>;

    // 需要通过信令通道发送出去的消息（offer / candidate），
    // 由外部转发
    using SignalOutCallback =
        std::function<void(const json &message)>;

    using StateCallback =
        std::function<void(rtc::PeerConnection::State)>;


    // ============================================================
    // 原来的接口
    // ============================================================

    void init(rtc::Configuration config);

    // 外部信令收到消息后调用此接口
    void handleSignalMessage(const json &message);

    void createPeerConnection();

    void createDataChannel(const std::string &label);

    void send(rtc::message_variant message);
    void send(const std::byte *data, size_t size);

    void close();

    void bindDataChannel(BinaryMessageCallback callback)
    {
        data_channel_binary_callback_ = std::move(callback);
    }

    void onSignalOut(SignalOutCallback callback)
    {
        signal_out_callback_ = std::move(callback);
    }

    void onStateChange(StateCallback callback)
    {
        state_change_callback_ = std::move(callback);
    }


    // ============================================================
    // 新增：Video
    // ============================================================

    bool createVideoTrack(
        const std::string &cname,
        const std::string &msid,
        uint8_t payload_type = 96,
        uint32_t ssrc = 0x10000001);

    bool sendVideoFrame(
        const rtc::binary &frame);


    // ============================================================
    // 新增：Audio
    // ============================================================

    bool createAudioTrack(
        const std::string &cname,
        const std::string &msid,
        uint8_t payload_type = 111,
        uint32_t ssrc = 0x10000002);

    bool sendAudioFrame(
        const rtc::binary &frame);


    // ============================================================
    // 新增：获取 Track
    // ============================================================

    std::shared_ptr<rtc::Track> videoTrack() const
    {
        return video_track_;
    }

    std::shared_ptr<rtc::Track> audioTrack() const
    {
        return audio_track_;
    }


private:

    // ============================================================
    // 原来的内部函数
    // ============================================================

    void createDataChannelPrivate(
        const std::string &label);

    void createDataChannelPrivate(
        std::shared_ptr<rtc::DataChannel> dc);

    void bindDataChannel();


    // ============================================================
    // 原来的成员
    // ============================================================

    std::shared_ptr<rtc::DataChannel> dc_;

    std::shared_ptr<rtc::PeerConnection> pc_;

    rtc::Configuration p2p_config_;


    BinaryMessageCallback data_channel_binary_callback_;

    SignalOutCallback signal_out_callback_;

    StateCallback state_change_callback_;


    bool is_closed_ = false;

    std::mutex mutex_;


    // ============================================================
    // 新增：Video
    // ============================================================

    std::shared_ptr<rtc::Track> video_track_;

    std::shared_ptr<rtc::RtpPacketizationConfig>
        video_rtp_config_;

    std::shared_ptr<rtc::RtcpSrReporter>
        video_sr_reporter_;


    // ============================================================
    // 新增：Audio
    // ============================================================

    std::shared_ptr<rtc::Track> audio_track_;

    std::shared_ptr<rtc::RtpPacketizationConfig>
        audio_rtp_config_;

    std::shared_ptr<rtc::RtcpSrReporter>
        audio_sr_reporter_;
};