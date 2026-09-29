// rd_codec.cpp —— OpenH264 编解码封装与 BGRA<->I420 颜色转换（跨平台）
#include "rd_codec.h"

#include <wels/codec_api.h>
#include <wels/codec_app_def.h>
#include <wels/codec_def.h>

#include <algorithm>
#include <cstring>

namespace rdcodec
{

// ---------------- 颜色转换（BT.601，整数运算） ----------------

void bgra_to_i420(const uint8_t *bgra, int w, int h, int stride_bgra,
                  uint8_t *y, int stride_y, uint8_t *u, uint8_t *v, int stride_c)
{
    for (int row = 0; row < h; row++)
    {
        const uint8_t *src = bgra + static_cast<size_t>(row) * stride_bgra;
        uint8_t *dst_y = y + static_cast<size_t>(row) * stride_y;

        for (int col = 0; col < w; col++)
        {
            int b = src[col * 4];
            int g = src[col * 4 + 1];
            int r = src[col * 4 + 2];

            dst_y[col] = static_cast<uint8_t>((66 * r + 129 * g + 25 * b + 128) >> 8);
        }
    }

    // UV 每 2x2 像素取均值
    for (int row = 0; row < h / 2; row++)
    {
        const uint8_t *s0 = bgra + static_cast<size_t>(row * 2) * stride_bgra;
        const uint8_t *s1 = s0 + stride_bgra;
        uint8_t *dst_u = u + static_cast<size_t>(row) * stride_c;
        uint8_t *dst_v = v + static_cast<size_t>(row) * stride_c;

        for (int col = 0; col < w / 2; col++)
        {
            int c0 = col * 8;
            int c1 = c0 + 4;

            int b = s0[c0] + s0[c1] + s1[c0] + s1[c1];
            int g = s0[c0 + 1] + s0[c1 + 1] + s1[c0 + 1] + s1[c1 + 1];
            int r = s0[c0 + 2] + s0[c1 + 2] + s1[c0 + 2] + s1[c1 + 2];

            b /= 4;
            g /= 4;
            r /= 4;

            dst_u[col] = static_cast<uint8_t>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            dst_v[col] = static_cast<uint8_t>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}

void i420_to_bgra(const uint8_t *y, int stride_y, const uint8_t *u, const uint8_t *v, int stride_c,
                  int w, int h, uint8_t *bgra, int stride_bgra)
{
    for (int row = 0; row < h; row++)
    {
        const uint8_t *src_y = y + static_cast<size_t>(row) * stride_y;
        const uint8_t *src_u = u + static_cast<size_t>(row / 2) * stride_c;
        const uint8_t *src_v = v + static_cast<size_t>(row / 2) * stride_c;
        uint8_t *dst = bgra + static_cast<size_t>(row) * stride_bgra;

        for (int col = 0; col < w; col++)
        {
            int yy = src_y[col] - 16;
            int uu = src_u[col / 2] - 128;
            int vv = src_v[col / 2] - 128;

            int r = (298 * yy + 409 * vv + 128) >> 8;
            int g = (298 * yy - 100 * uu - 208 * vv + 128) >> 8;
            int b = (298 * yy + 516 * uu + 128) >> 8;

            dst[col * 4] = static_cast<uint8_t>(std::clamp(b, 0, 255));
            dst[col * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
            dst[col * 4 + 2] = static_cast<uint8_t>(std::clamp(r, 0, 255));
            dst[col * 4 + 3] = 255;
        }
    }
}

// ---------------- H.264 编码器 ----------------

H264Encoder::~H264Encoder()
{
    close();
}

bool H264Encoder::open(int w, int h, int fps, int bitrate_bps, bool screen)
{
    close();

    if (WelsCreateSVCEncoder(&enc_) != 0 || !enc_)
    {
        enc_ = nullptr;
        return false;
    }

    SEncParamExt param;
    std::memset(&param, 0, sizeof(param));
    enc_->GetDefaultParams(&param);

    param.iUsageType = screen ? SCREEN_CONTENT_REAL_TIME : CAMERA_VIDEO_REAL_TIME;
    param.iPicWidth = w;
    param.iPicHeight = h;
    param.iTargetBitrate = bitrate_bps;
    param.iRCMode = RC_BITRATE_MODE;
    param.fMaxFrameRate = static_cast<float>(fps);
    param.uiIntraPeriod = static_cast<unsigned int>(fps * 2); // 每 2 秒一个 IDR
    param.bEnableFrameSkip = false;
    param.iMaxQp = 42;

    param.sSpatialLayers[0].iVideoWidth = w;
    param.sSpatialLayers[0].iVideoHeight = h;
    param.sSpatialLayers[0].fFrameRate = static_cast<float>(fps);
    param.sSpatialLayers[0].iSpatialBitrate = bitrate_bps;
    param.sSpatialLayers[0].uiProfileIdc = PRO_BASELINE;

    if (enc_->InitializeExt(&param) != cmResultSuccess)
    {
        WelsDestroySVCEncoder(enc_);
        enc_ = nullptr;
        return false;
    }

    w_ = w;
    h_ = h;
    fps_ = fps;
    stride_y_ = (w + 31) & ~31;
    stride_c_ = stride_y_ / 2;
    y_.assign(static_cast<size_t>(stride_y_) * h, 0);
    u_.assign(static_cast<size_t>(stride_c_) * (h / 2), 128);
    v_.assign(static_cast<size_t>(stride_c_) * (h / 2), 128);
    ts_ = 0;
    return true;
}

void H264Encoder::close()
{
    if (enc_)
    {
        enc_->Uninitialize();
        WelsDestroySVCEncoder(enc_);
        enc_ = nullptr;
    }
    y_.clear();
    u_.clear();
    v_.clear();
}

bool H264Encoder::encode(const uint8_t *bgra, std::vector<uint8_t> &out, bool &keyframe)
{
    out.clear();
    keyframe = false;
    if (!enc_)
        return false;

    bgra_to_i420(bgra, w_, h_, w_ * 4,
                 y_.data(), stride_y_, u_.data(), v_.data(), stride_c_);

    SSourcePicture pic;
    std::memset(&pic, 0, sizeof(pic));
    pic.iColorFormat = videoFormatI420;
    pic.iPicWidth = w_;
    pic.iPicHeight = h_;
    pic.iStride[0] = stride_y_;
    pic.iStride[1] = stride_c_;
    pic.iStride[2] = stride_c_;
    pic.pData[0] = y_.data();
    pic.pData[1] = u_.data();
    pic.pData[2] = v_.data();
    pic.uiTimeStamp = static_cast<long long>(ts_ * 1000 / fps_);
    ts_++;

    SFrameBSInfo info;
    std::memset(&info, 0, sizeof(info));
    if (enc_->EncodeFrame(&pic, &info) != cmResultSuccess)
        return false;

    if (info.iFrameSizeInBytes <= 0)
        return false; // 无输出

    keyframe = info.eFrameType == videoFrameTypeIDR;

    // 各层按 NAL 长度拼接为完整帧码流
    size_t total = 0;
    for (int layer = 0; layer < info.iLayerNum; layer++)
    {
        const SLayerBSInfo &ls = info.sLayerInfo[layer];
        for (int nal = 0; nal < ls.iNalCount; nal++)
            total += static_cast<size_t>(ls.pNalLengthInByte[nal]);
    }
    out.reserve(total);
    for (int layer = 0; layer < info.iLayerNum; layer++)
    {
        const SLayerBSInfo &ls = info.sLayerInfo[layer];
        int layer_size = 0;
        for (int nal = 0; nal < ls.iNalCount; nal++)
            layer_size += ls.pNalLengthInByte[nal];
        out.insert(out.end(), ls.pBsBuf, ls.pBsBuf + layer_size);
    }

    return !out.empty();
}

// ---------------- H.264 解码器 ----------------

H264Decoder::~H264Decoder()
{
    close();
}

bool H264Decoder::open()
{
    close();

    if (WelsCreateDecoder(&dec_) != 0 || !dec_)
    {
        dec_ = nullptr;
        return false;
    }

    SDecodingParam param;
    std::memset(&param, 0, sizeof(param));
    param.uiTargetDqLayer = 255;
    param.eEcActiveIdc = ERROR_CON_SLICE_COPY;
    param.bParseOnly = false;
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;

    if (dec_->Initialize(&param) != cmResultSuccess)
    {
        WelsDestroyDecoder(dec_);
        dec_ = nullptr;
        return false;
    }
    return true;
}

void H264Decoder::close()
{
    if (dec_)
    {
        dec_->Uninitialize();
        WelsDestroyDecoder(dec_);
        dec_ = nullptr;
    }
}

bool H264Decoder::decode(const uint8_t *data, size_t len, std::vector<uint8_t> &bgra, int &w, int &h)
{
    if (!dec_ || !data || len == 0)
        return false;

    SBufferInfo info;
    std::memset(&info, 0, sizeof(info));
    unsigned char *p_dst[3] = {nullptr, nullptr, nullptr};

    DECODING_STATE st = dec_->DecodeFrameNoDelay(
        data, static_cast<int>(len), p_dst, &info);
    if (st & dsErrorFree)
    {
        // 轻微码流错误也容忍，只看是否有帧输出
    }

    if (info.iBufferStatus != 1 || !p_dst[0])
        return false;

    w = info.UsrData.sSystemBuffer.iWidth;
    h = info.UsrData.sSystemBuffer.iHeight;
    if (w <= 0 || h <= 0)
        return false;

    int stride_y = info.UsrData.sSystemBuffer.iStride[0];
    int stride_c = info.UsrData.sSystemBuffer.iStride[1];

    bgra.resize(static_cast<size_t>(w) * h * 4);
    i420_to_bgra(p_dst[0], stride_y, p_dst[1], p_dst[2], stride_c,
                 w, h, bgra.data(), w * 4);
    return true;
}

} // namespace rdcodec
