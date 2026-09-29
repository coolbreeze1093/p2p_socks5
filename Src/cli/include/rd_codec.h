// rd_codec.h —— 远程桌面 H.264 编解码封装（跨平台，基于 OpenH264）
//
// 颜色转换(BGRA<->I420)与 H.264 编解码均为纯 C++ 实现，不依赖平台 API；
// 屏幕捕获(GDI)与显示/注入(Win32)仍留在 rd_server/rd_viewer 中。
#pragma once
#include <cstdint>
#include <vector>

#include <wels/codec_api.h>

namespace rdcodec
{

// BGRA(top-down, 4 字节/像素) -> I420 平面格式
void bgra_to_i420(const uint8_t *bgra, int w, int h, int stride_bgra,
                  uint8_t *y, int stride_y, uint8_t *u, uint8_t *v, int stride_c);

// I420 平面格式 -> BGRA(top-down)
void i420_to_bgra(const uint8_t *y, int stride_y, const uint8_t *u, const uint8_t *v, int stride_c,
                  int w, int h, uint8_t *bgra, int stride_bgra);

class H264Encoder
{
public:
    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder &) = delete;
    H264Encoder &operator=(const H264Encoder &) = delete;

    // screen=true 使用屏幕内容优化模式
    bool open(int w, int h, int fps, int bitrate_bps, bool screen);
    void close();
    bool is_open() const { return enc_ != nullptr; }

    // 输入 BGRA(top-down, stride = w*4)；输出该帧 Annex-B 码流，keyframe 标记 IDR
    bool encode(const uint8_t *bgra, std::vector<uint8_t> &out, bool &keyframe);

private:
    ISVCEncoder *enc_ = nullptr;
    int w_ = 0, h_ = 0, fps_ = 0;
    int stride_y_ = 0, stride_c_ = 0;
    std::vector<uint8_t> y_, u_, v_;
    uint64_t ts_ = 0;
};

class H264Decoder
{
public:
    H264Decoder() = default;
    ~H264Decoder();
    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    bool open();
    void close();
    bool is_open() const { return dec_ != nullptr; }

    // 输入一段 Annex-B 码流（可含多个 NAL）；产出解码后的 BGRA(top-down)
    bool decode(const uint8_t *data, size_t len, std::vector<uint8_t> &bgra, int &w, int &h);

private:
    ISVCDecoder *dec_ = nullptr;
};

} // namespace rdcodec
