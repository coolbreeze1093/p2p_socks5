#include "p2p_client.h"

#include <plog/Log.h>


// ================================================================
// 原来的 init()
// ================================================================

void P2PClient::init(rtc::Configuration config)
{
    p2p_config_ = config;
}


// ================================================================
// 原来的 createDataChannel()
// ================================================================

void P2PClient::createDataChannel(const std::string &label)
{
    PLOG_DEBUG << "createDataChannel: " << label;

    createDataChannelPrivate(label);
}


// ================================================================
// 原来的 handleSignalMessage()
// ================================================================

void P2PClient::handleSignalMessage(const json &message_json)
{
    std::string type =
        message_json.value("type", "");

    if (type == "description")
    {
        if (message_json.find("description")
            == message_json.end())
        {
            PLOG_ERROR << "No sdp";
            return;
        }

        std::string sdp =
            message_json["description"];

        if (pc_)
        {
            pc_->setRemoteDescription(
                rtc::Description(sdp, type));
        }
    }
    else if (type == "candidate")
    {
        if (message_json.find("candidate")
            == message_json.end())
        {
            PLOG_ERROR
                << "Error: No candidate field in Candidate message";

            return;
        }

        if (message_json.find("mid")
            == message_json.end())
        {
            PLOG_ERROR
                << "Error: No mid field in Candidate message";

            return;
        }

        std::string candidate =
            message_json["candidate"];

        std::string mid =
            message_json["mid"];

        if (pc_)
        {
            pc_->addRemoteCandidate(
                rtc::Candidate(candidate, mid));
        }
    }
    else
    {
        PLOG_ERROR
            << "P2PClient: unhandled message type: "
            << type;
    }
}


// ================================================================
// 原来的 send()
// ================================================================

void P2PClient::send(rtc::message_variant message)
{
    try
    {
        if (dc_ && dc_->isOpen())
        {
            dc_->send(message);
        }
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: send message: "
            << e.what();
    }
}


// ================================================================
// 原来的 send()
// ================================================================

void P2PClient::send(
    const std::byte *data,
    size_t size)
{
    try
    {
        if (dc_ && dc_->isOpen())
        {
            dc_->send(data, size);
        }
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: send data: "
            << e.what();
    }
}


// ================================================================
// 原来的 close()
// ================================================================

void P2PClient::close()
{
    try
    {
        if (dc_)
        {
            dc_->close();
        }

        if (pc_)
        {
            pc_->close();
        }
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: close: "
            << e.what();
    }
}


// ================================================================
// 原来的 bindDataChannel()
// ================================================================

void P2PClient::bindDataChannel()
{
    dc_->onOpen(
        [this]()
        {
            PLOG_DEBUG
                << "DataChannel opened"
                << "max message size: "
                << dc_->maxMessageSize();
        });


    dc_->onClosed(
        []()
        {
            PLOG_INFO
                << "DataChannel closed";
        });


    dc_->onError(
        [](std::string message)
        {
            PLOG_ERROR
                << "DataChannel error: "
                << message;
        });


    dc_->onMessage(
        [this](rtc::binary message)
        {
            if (data_channel_binary_callback_)
            {
                data_channel_binary_callback_(message);
            }
            else
            {
                PLOG_INFO
                    << "rev Data "
                    << "data_channel_binary_callback_ is null";
            }
        },

        [](std::string message)
        {
            PLOG_INFO
                << "DataChannel message: "
                << message;
        });
}


// ================================================================
// 原来的 createDataChannelPrivate()
// ================================================================

void P2PClient::createDataChannelPrivate(
    const std::string &label)
{
    try
    {
        dc_ =
            pc_->createDataChannel(label);

        bindDataChannel();
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: createDataChannel: "
            << e.what();
    }
}


// ================================================================
// 原来的 createDataChannelPrivate()
// ================================================================

void P2PClient::createDataChannelPrivate(
    std::shared_ptr<rtc::DataChannel> dc)
{
    dc_ = dc;

    bindDataChannel();
}


// ================================================================
// 原来的 createPeerConnection()
// ================================================================

void P2PClient::createPeerConnection()
{
    PLOG_DEBUG << "**createPeerConnection";

    try
    {
        pc_ =
            std::make_shared<rtc::PeerConnection>(
                p2p_config_);


        pc_->onStateChange(
            [this](rtc::PeerConnection::State state)
            {
                PLOG_INFO
                    << "PeerConnection state: "
                    << static_cast<int>(state);

                if (state_change_callback_)
                {
                    state_change_callback_(state);
                }
            });


        pc_->onGatheringStateChange(
            [](rtc::PeerConnection::GatheringState state)
            {
                PLOG_INFO
                    << "Gathering state: "
                    << static_cast<int>(state);
            });


        pc_->onLocalCandidate(
            [this](rtc::Candidate candidate)
            {
                json j;

                j["type"] =
                    "candidate";

                j["candidate"] =
                    candidate;

                j["mid"] =
                    candidate.mid();

                PLOG_INFO
                    << "Local candidate: "
                    << candidate;

                if (signal_out_callback_)
                {
                    signal_out_callback_(j);
                }
            });


        pc_->onLocalDescription(
            [this](rtc::Description description)
            {
                json j;

                j["type"] =
                    "description";

                j["description"] =
                    description;

                PLOG_INFO
                    << "Local description: "
                    << description;

                if (signal_out_callback_)
                {
                    signal_out_callback_(j);
                }
            });


        pc_->onDataChannel(
            [this](
                std::shared_ptr<rtc::DataChannel> data_channel)
            {
                PLOG_INFO
                    << "DataChannel opened"
                    << "max message size: "
                    << data_channel->maxMessageSize();

                createDataChannelPrivate(
                    data_channel);
            });
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: createPeerConnection: "
            << e.what();
    }
}


// ============================================================================
//                              新增 Video
// ============================================================================

bool P2PClient::createVideoTrack(
    const std::string &cname,
    const std::string &msid,
    uint8_t payload_type,
    uint32_t ssrc)
{
    if (!pc_)
    {
        PLOG_ERROR
            << "P2PClient: createVideoTrack: "
            << "PeerConnection is null";

        return false;
    }

    try
    {
        // ------------------------------------------------------------
        // Video Description
        // ------------------------------------------------------------

        auto video =
            rtc::Description::Video(cname);

        video.addH264Codec(
            payload_type);

        video.addSSRC(
            ssrc,
            cname,
            msid,
            cname);


        // ------------------------------------------------------------
        // 创建 Track
        // ------------------------------------------------------------

        video_track_ =
            pc_->addTrack(video);

        if (!video_track_)
        {
            PLOG_ERROR
                << "P2PClient: failed to create video track";

            return false;
        }


        // ------------------------------------------------------------
        // RTP configuration
        // ------------------------------------------------------------

        video_rtp_config_ =
            std::make_shared<
                rtc::RtpPacketizationConfig>(
                    ssrc,
                    cname,
                    payload_type,
                    rtc::H264RtpPacketizer::ClockRate);


        // ------------------------------------------------------------
        // H264 RTP Packetizer
        // ------------------------------------------------------------

        auto packetizer =
            std::make_shared<
                rtc::H264RtpPacketizer>(
                    rtc::NalUnit::Separator::Length,
                    video_rtp_config_);


        // ------------------------------------------------------------
        // RTCP Sender Report
        // ------------------------------------------------------------

        video_sr_reporter_ =
            std::make_shared<
                rtc::RtcpSrReporter>(
                    video_rtp_config_);

        packetizer->addToChain(
            video_sr_reporter_);


        // ------------------------------------------------------------
        // RTCP NACK
        // ------------------------------------------------------------

        auto nack_responder =
            std::make_shared<
                rtc::RtcpNackResponder>();

        packetizer->addToChain(
            nack_responder);


        // ------------------------------------------------------------
        // 设置 MediaHandler
        // ------------------------------------------------------------

        video_track_->setMediaHandler(
            packetizer);


        PLOG_INFO
            << "P2PClient: video track created";

        return true;
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: createVideoTrack: "
            << e.what();

        return false;
    }
}


// ============================================================================
//                              新增 Audio
// ============================================================================

bool P2PClient::createAudioTrack(
    const std::string &cname,
    const std::string &msid,
    uint8_t payload_type,
    uint32_t ssrc)
{
    if (!pc_)
    {
        PLOG_ERROR
            << "P2PClient: createAudioTrack: "
            << "PeerConnection is null";

        return false;
    }

    try
    {
        // ------------------------------------------------------------
        // Audio Description
        // ------------------------------------------------------------

        auto audio =
            rtc::Description::Audio(cname);

        audio.addOpusCodec(
            payload_type);

        audio.addSSRC(
            ssrc,
            cname,
            msid,
            cname);


        // ------------------------------------------------------------
        // 创建 Track
        // ------------------------------------------------------------

        audio_track_ =
            pc_->addTrack(audio);

        if (!audio_track_)
        {
            PLOG_ERROR
                << "P2PClient: failed to create audio track";

            return false;
        }


        // ------------------------------------------------------------
        // RTP configuration
        // ------------------------------------------------------------

        audio_rtp_config_ =
            std::make_shared<
                rtc::RtpPacketizationConfig>(
                    ssrc,
                    cname,
                    payload_type,
                    rtc::OpusRtpPacketizer::DefaultClockRate);


        // ------------------------------------------------------------
        // Opus RTP Packetizer
        // ------------------------------------------------------------

        auto packetizer =
            std::make_shared<
                rtc::OpusRtpPacketizer>(
                    audio_rtp_config_);


        // ------------------------------------------------------------
        // RTCP Sender Report
        // ------------------------------------------------------------

        audio_sr_reporter_ =
            std::make_shared<
                rtc::RtcpSrReporter>(
                    audio_rtp_config_);

        packetizer->addToChain(
            audio_sr_reporter_);


        // ------------------------------------------------------------
        // RTCP NACK
        // ------------------------------------------------------------

        auto nack_responder =
            std::make_shared<
                rtc::RtcpNackResponder>();

        packetizer->addToChain(
            nack_responder);


        // ------------------------------------------------------------
        // 设置 MediaHandler
        // ------------------------------------------------------------

        audio_track_->setMediaHandler(
            packetizer);


        PLOG_INFO
            << "P2PClient: audio track created";

        return true;
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: createAudioTrack: "
            << e.what();

        return false;
    }
}


// ============================================================================
//                         新增 Video Frame 发送
// ============================================================================

bool P2PClient::sendVideoFrame(
    const rtc::binary &frame)
{
    try
    {
        if (!video_track_)
        {
            PLOG_ERROR
                << "P2PClient: video track is null";

            return false;
        }

        if (!video_track_->isOpen())
        {
            PLOG_DEBUG
                << "P2PClient: video track is not open";

            return false;
        }


        // 注意：
        // frame 必须已经是 H264 编码后的数据。
        //
        // 不是 YUV。
        // 不是 RGB。
        //
        // 当前 H264 Packetizer 配置使用：
        // NalUnit::Separator::Length
        //
        // 因此 frame 应该是 length-prefixed H264 sample。

        video_track_->send(
            frame);

        return true;
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: sendVideoFrame: "
            << e.what();

        return false;
    }
}


// ============================================================================
//                         新增 Audio Frame 发送
// ============================================================================

bool P2PClient::sendAudioFrame(
    const rtc::binary &frame)
{
    try
    {
        if (!audio_track_)
        {
            PLOG_ERROR
                << "P2PClient: audio track is null";

            return false;
        }

        if (!audio_track_->isOpen())
        {
            PLOG_DEBUG
                << "P2PClient: audio track is not open";

            return false;
        }


        // 注意：
        // frame 必须已经是 Opus 编码后的音频帧。
        //
        // 不是 PCM。

        audio_track_->send(frame);

        return true;
    }
    catch (const std::exception &e)
    {
        PLOG_ERROR
            << "P2PClient: sendAudioFrame: "
            << e.what();

        return false;
    }
}