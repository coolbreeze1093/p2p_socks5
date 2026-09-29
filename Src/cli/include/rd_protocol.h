// rd_protocol.h —— 远程桌面(RD)私有协议（rtcsocks 命令行版）
//
// 跑在 label 为 "rd" 的独立 DataChannel 上，与代理业务的 "data" 通道互不影响：
//   - 控制端(rtcsocks viewer) -> 被控端(rtcsocks-exit)：Start/Stop、鼠标键盘输入
//   - 被控端 -> 控制端：屏幕帧(JPEG)、应答/错误
//
// 消息格式: [msg_type:1B][payload_len:4B 大端][payload]
// DataChannel 本身保证消息边界，无需流式重组。
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace rd
{

constexpr const char *kChannelLabel = "rd";

enum MsgType : uint8_t
{
    // -------- 控制端 -> 被控端 --------
    MouseMove = 0x01,   // payload: x(2B) y(2B)，归一化 0-65535（与 SendInput 绝对坐标一致）
    MouseButton = 0x02, // payload: flags(1B) down(1B)；flags: left=1 right=2 middle=4
    MouseWheel = 0x03,  // payload: delta(2B, 有符号)
    Key = 0x04,         // payload: vk(1B) down(1B)
    Start = 0x05,       // payload: fps(1B) quality(1B) max_width(2B) codec(1B)
    Stop = 0x06,        // payload 为空

    // -------- 被控端 -> 控制端 --------
    Frame = 0x81,   // JPEG 帧: width(2B) height(2B) + jpeg 数据
    FrameH264 = 0x85, // H.264 帧: width(2B) height(2B) flags(1B, bit0=IDR) + Annex-B 码流
    Started = 0x82, // payload 为空
    Stopped = 0x83, // payload 为空
    Error = 0x84,   // payload: code(1B)
};

enum Codec : uint8_t
{
    CodecAuto = 0, // 被控端自行选择（优先 H.264，失败回退 JPEG）
    CodecH264 = 1,
    CodecJpeg = 2,
};

enum ErrorCode : uint8_t
{
    ErrDisabled = 1, // 被控端未开启 --rd
    ErrCapture = 2,  // 抓屏/编码失败
};

inline void put16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

inline uint16_t get16(const uint8_t *p)
{
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

inline uint32_t get32(const uint8_t *p)
{
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline std::vector<uint8_t> make_msg(uint8_t type, const uint8_t *payload, size_t len)
{
    std::vector<uint8_t> m(5 + len);
    m[0] = type;
    put32(&m[1], static_cast<uint32_t>(len));
    if (len)
        std::memcpy(&m[5], payload, len);
    return m;
}

} // namespace rd
