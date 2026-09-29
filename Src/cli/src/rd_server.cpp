// rd_server.cpp —— 远程桌面被控端实现（仅 Windows）
#include "rd_server.h"
#ifdef _WIN32

#include "rd_protocol.h"
#include <plog/Log.h>

#include <windows.h>
#include <gdiplus.h>

#include <chrono>
#include <cstring>
#include <cwchar>

// 单帧 JPEG 上限：DataChannel max message size 为 256KB，留出余量；
// 超限时自动降低质量重编码
static constexpr size_t kMaxJpegSize = 240000;

namespace
{

using namespace Gdiplus;

int get_jpeg_encoder(CLSID *clsid)
{
    UINT num = 0, size = 0;
    if (GetImageEncodersSize(&num, &size) != Ok || size == 0)
        return -1;

    std::vector<BYTE> buf(size);
    if (GetImageEncoders(num, size, reinterpret_cast<ImageCodecInfo *>(buf.data())) != Ok)
        return -1;

    auto *encoders = reinterpret_cast<ImageCodecInfo *>(buf.data());
    for (UINT i = 0; i < num; i++)
    {
        if (wcscmp(encoders[i].MimeType, L"image/jpeg") == 0)
        {
            *clsid = encoders[i].Clsid;
            return 0;
        }
    }
    return -1;
}

} // namespace

RdServer::~RdServer()
{
    stop();
}

void RdServer::send_msg(const uint8_t *data, size_t len)
{
    if (send_)
        send_(data, len);
}

void RdServer::handle_input(const uint8_t *data, size_t len)
{
    if (len < 5)
        return;

    uint8_t type = data[0];
    uint32_t plen = rd::get32(data + 1);
    if (static_cast<size_t>(plen) + 5 != len)
        return;

    const uint8_t *p = data + 5;
    switch (type)
    {
    case rd::MouseMove:
        inject_mouse_move(p, plen);
        break;
    case rd::MouseButton:
        inject_mouse_button(p, plen);
        break;
    case rd::MouseWheel:
        inject_mouse_wheel(p, plen);
        break;
    case rd::Key:
        inject_key(p, plen);
        break;
    case rd::Start:
        if (plen >= 5)
        {
            uint8_t codec = plen >= 6 ? p[5] : rd::CodecAuto;
            start_stream(p[0], p[1], rd::get16(p + 2), codec);
        }
        break;
    case rd::Stop:
        stop();
        break;
    default:
        break;
    }
}

void RdServer::start_stream(uint8_t fps, uint8_t quality, uint16_t max_width, uint8_t codec)
{
    stop();

    fps_ = fps ? fps : 8;
    quality_ = quality ? quality : 60;
    max_width_ = max_width ? max_width : 1280;
    codec_ = codec;

    PLOG_INFO << "rd: stream start request, fps=" << (int)fps_
              << " quality=" << (int)quality_ << " max_width=" << max_width_
              << " codec=" << (int)codec_;

    running_ = true;
    thread_ = std::thread(&RdServer::capture_loop, this);

    auto m = rd::make_msg(rd::Started, nullptr, 0);
    send_msg(m.data(), m.size());
}

void RdServer::stop()
{
    bool was_running = running_.exchange(false);
    if (thread_.joinable())
        thread_.join();

    if (was_running)
    {
        auto m = rd::make_msg(rd::Stopped, nullptr, 0);
        send_msg(m.data(), m.size());
        PLOG_INFO << "rd: stream stopped";
    }
}

void RdServer::capture_loop()
{
    ULONG_PTR gdiplus_token = 0;
    GdiplusStartupInput gdiplus_input;
    if (GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Ok)
    {
        PLOG_ERROR << "rd: GdiplusStartup failed";
        uint8_t code = rd::ErrCapture;
        auto m = rd::make_msg(rd::Error, &code, 1);
        send_msg(m.data(), m.size());
        return;
    }

    CLSID jpeg_clsid;
    if (get_jpeg_encoder(&jpeg_clsid) != 0)
    {
        PLOG_ERROR << "rd: jpeg encoder not found";
        GdiplusShutdown(gdiplus_token);
        uint8_t code = rd::ErrCapture;
        auto m = rd::make_msg(rd::Error, &code, 1);
        send_msg(m.data(), m.size());
        return;
    }

    // 主屏抓屏，超宽时缩放到 max_width_（保持纵横比）；宽高取偶（I420 要求）
    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);
    int dw = sw;
    int dh = sh;
    if (dw > max_width_)
    {
        dh = sh * max_width_ / sw;
        dw = max_width_;
    }
    dw &= ~1;
    dh &= ~1;

    // H.264：OpenH264 屏幕内容模式；打开失败则回退 JPEG
    use_h264_ = codec_ != rd::CodecJpeg &&
                encoder_.open(dw, dh, fps_, 1500000, true);
    if (use_h264_)
    {
        PLOG_INFO << "rd: codec = h264 (openh264), " << dw << "x" << dh;
    }
    else
    {
        PLOG_INFO << "rd: codec = jpeg (h264 unavailable or disabled)";
    }

    HDC screen_dc = GetDC(nullptr);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HBITMAP mem_bmp = CreateCompatibleBitmap(screen_dc, dw, dh);
    HGDIOBJ old_bmp = SelectObject(mem_dc, mem_bmp);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = dw;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<uint8_t> pixels(static_cast<size_t>(dw) * dh * 4);
    std::vector<uint8_t> prev;

    const auto interval = std::chrono::milliseconds(1000 / fps_);
    auto next_tick = std::chrono::steady_clock::now();
    uint64_t frames = 0;

    PLOG_INFO << "rd: capturing " << sw << "x" << sh << " -> " << dw << "x" << dh;

    while (running_)
    {
        if (!StretchBlt(mem_dc, 0, 0, dw, dh, screen_dc, 0, 0, sw, sh, SRCCOPY | CAPTUREBLT))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // GetDIBits 会改写 biHeight，每次都置回负值（top-down，行序与 GDI+ 一致）
        bmi.bmiHeader.biHeight = -dh;
        if (GetDIBits(mem_dc, mem_bmp, 0, dh, pixels.data(), &bmi, DIB_RGB_COLORS) != dh)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 画面未变化则跳过编码发送
        if (pixels.size() == prev.size() && std::memcmp(pixels.data(), prev.data(), pixels.size()) == 0)
        {
            prev = pixels;
            goto pace;
        }
        prev = pixels;

        {
            if (use_h264_)
            {
                std::vector<uint8_t> bitstream;
                bool keyframe = false;
                if (encoder_.encode(pixels.data(), bitstream, keyframe))
                {
                    uint8_t head[5];
                    rd::put16(head, static_cast<uint16_t>(dw));
                    rd::put16(head + 2, static_cast<uint16_t>(dh));
                    head[4] = keyframe ? 0x01 : 0x00;

                    std::vector<uint8_t> payload(5 + bitstream.size());
                    std::memcpy(payload.data(), head, 5);
                    std::memcpy(payload.data() + 5, bitstream.data(), bitstream.size());

                    auto m = rd::make_msg(rd::FrameH264, payload.data(), payload.size());
                    send_msg(m.data(), m.size());

                    frames++;
                    if (frames == 1 || frames % 100 == 0)
                    {
                        PLOG_INFO << "rd: frame #" << frames << " sent, "
                                  << dw << "x" << dh << ", h264 " << bitstream.size()
                                  << " bytes" << (keyframe ? " (IDR)" : "");
                    }
                }
                else
                {
                    PLOG_ERROR << "rd: h264 encode failed";
                }
            }
            else
            {
                std::vector<uint8_t> jpeg;
                if (encode_jpeg(pixels, dw, dh, quality_, jpeg))
                {
                    uint8_t head[4];
                    rd::put16(head, static_cast<uint16_t>(dw));
                    rd::put16(head + 2, static_cast<uint16_t>(dh));

                    std::vector<uint8_t> payload(4 + jpeg.size());
                    std::memcpy(payload.data(), head, 4);
                    std::memcpy(payload.data() + 4, jpeg.data(), jpeg.size());

                    auto m = rd::make_msg(rd::Frame, payload.data(), payload.size());
                    send_msg(m.data(), m.size());

                    frames++;
                    if (frames == 1 || frames % 100 == 0)
                    {
                        PLOG_INFO << "rd: frame #" << frames << " sent, "
                                  << dw << "x" << dh << ", " << jpeg.size() << " bytes";
                    }
                }
                else
                {
                    PLOG_ERROR << "rd: jpeg encode failed";
                }
            }
        }

    pace:
        next_tick += interval;
        auto now = std::chrono::steady_clock::now();
        if (next_tick < now) // 追不上帧率时重新对齐
            next_tick = now;
        while (running_ && std::chrono::steady_clock::now() < next_tick)
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    SelectObject(mem_dc, old_bmp);
    DeleteObject(mem_bmp);
    DeleteDC(mem_dc);
    ReleaseDC(nullptr, screen_dc);
    encoder_.close();
    GdiplusShutdown(gdiplus_token);
}

bool RdServer::encode_jpeg(const std::vector<uint8_t> &bgra, int w, int h,
                           uint8_t quality, std::vector<uint8_t> &out)
{
    CLSID jpeg_clsid;
    if (get_jpeg_encoder(&jpeg_clsid) != 0)
        return false;

    // 超过单帧上限时逐档降低质量重编
    for (int q = quality; q >= 15; q -= 15)
    {
        Bitmap bmp(w, h, w * 4, PixelFormat32bppRGB,
                   const_cast<BYTE *>(bgra.data()));
        if (bmp.GetLastStatus() != Ok)
            return false;

        IStream *stream = nullptr;
        if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK)
            return false;

        bool ok = false;
        EncoderParameters ep{};
        ep.Count = 1;
        ep.Parameter[0].Guid = EncoderQuality;
        ep.Parameter[0].Type = EncoderParameterValueTypeLong;
        ep.Parameter[0].NumberOfValues = 1;
        ULONG qv = static_cast<ULONG>(q);
        ep.Parameter[0].Value = &qv;

        if (bmp.Save(stream, &jpeg_clsid, &ep) == Ok)
        {
            STATSTG stat{};
            if (stream->Stat(&stat, STATFLAG_NONAME) == S_OK &&
                stat.cbSize.LowPart <= kMaxJpegSize)
            {
                HGLOBAL hg = nullptr;
                if (GetHGlobalFromStream(stream, &hg) == S_OK)
                {
                    void *ptr = GlobalLock(hg);
                    if (ptr)
                    {
                        out.assign(static_cast<uint8_t *>(ptr),
                                   static_cast<uint8_t *>(ptr) + stat.cbSize.LowPart);
                        GlobalUnlock(hg);
                        ok = true;
                    }
                }
            }
        }
        stream->Release();

        if (ok)
            return true;
    }
    return false;
}

void RdServer::inject_mouse_move(const uint8_t *p, uint32_t plen)
{
    if (plen < 4)
        return;

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    in.mi.dx = rd::get16(p);
    in.mi.dy = rd::get16(p + 2);
    SendInput(1, &in, sizeof(INPUT));
}

void RdServer::inject_mouse_button(const uint8_t *p, uint32_t plen)
{
    if (plen < 2)
        return;

    uint8_t flags = p[0];
    bool down = p[1] != 0;

    INPUT in{};
    in.type = INPUT_MOUSE;
    if (flags & 0x01)
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    else if (flags & 0x02)
        in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    else if (flags & 0x04)
        in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    else
        return;

    SendInput(1, &in, sizeof(INPUT));
}

void RdServer::inject_mouse_wheel(const uint8_t *p, uint32_t plen)
{
    if (plen < 2)
        return;

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    in.mi.mouseData = static_cast<DWORD>(static_cast<int16_t>(rd::get16(p)));
    SendInput(1, &in, sizeof(INPUT));
}

void RdServer::inject_key(const uint8_t *p, uint32_t plen)
{
    if (plen < 2)
        return;

    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = p[0];
    in.ki.dwFlags = p[1] ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

#endif // _WIN32
