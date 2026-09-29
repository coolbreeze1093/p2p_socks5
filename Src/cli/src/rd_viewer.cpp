// rd_viewer.cpp —— 远程桌面控制端实现（仅 Windows）
#include "rd_viewer.h"
#ifdef _WIN32

#include "rd_protocol.h"
#include <plog/Log.h>

#include <gdiplus.h>

#include <algorithm>

namespace
{
const wchar_t *kClassName = L"rtcsocks_rd_viewer";
}

RdViewer::~RdViewer()
{
    stop();
}

bool RdViewer::start(SendFunc send, uint16_t fps, uint16_t quality, uint16_t max_width)
{
    if (running_.exchange(true))
        return true; // 已在运行

    send_ = std::move(send);
    fps_ = fps;
    quality_ = quality;
    max_width_ = max_width;

    thread_ = std::thread(&RdViewer::run, this);
    return true;
}

void RdViewer::stop()
{
    if (!running_.exchange(false))
        return;

    if (hwnd_)
        PostMessage(hwnd_, WM_CLOSE, 0, 0); // 跨线程安全

    if (thread_.joinable())
        thread_.join();
    hwnd_ = nullptr;
}

void RdViewer::run()
{
    ULONG_PTR gdiplus_token = 0;
    Gdiplus::GdiplusStartupInput gdiplus_input;
    if (Gdiplus::GdiplusStartup(&gdiplus_token, &gdiplus_input, nullptr) != Gdiplus::Ok)
    {
        PLOG_ERROR << "rd viewer: GdiplusStartup failed";
        running_ = false;
        return;
    }

    // 请求被控端开始推流（无论有无窗口都发送）
    {
        uint8_t pl[6];
        pl[0] = static_cast<uint8_t>(fps_ > 60 ? 60 : fps_);
        pl[1] = static_cast<uint8_t>(quality_ ? quality_ : 60);
        rd::put16(pl + 2, max_width_ ? max_width_ : 1280);
        pl[5] = rd::CodecH264;
        auto m = rd::make_msg(rd::Start, pl, sizeof(pl));
        if (send_)
            send_(m.data(), m.size());
    }

    bool window_ok = true;
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &RdViewer::wndproc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClassName;
    if (!RegisterClassW(&wc))
    {
        PLOG_WARNING << "rd viewer: RegisterClass failed, error=" << GetLastError();
        window_ok = false;
    }

    if (window_ok)
    {
        hwnd_ = CreateWindowExW(0, kClassName, L"rtcsocks remote desktop",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                800, 500, nullptr, nullptr,
                                GetModuleHandle(nullptr), this);
        if (!hwnd_)
        {
            PLOG_WARNING << "rd viewer: CreateWindow failed, error=" << GetLastError()
                         << ", running headless (decode only)";
            window_ok = false;
        }
    }

    window_ok_ = window_ok;

    if (window_ok)
    {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        PLOG_INFO << "rd viewer: window created";

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // 关闭窗口即停止推流
        auto m = rd::make_msg(rd::Stop, nullptr, 0);
        if (send_)
            send_(m.data(), m.size());

        hwnd_ = nullptr;
        UnregisterClassW(kClassName, GetModuleHandle(nullptr));
    }
    else
    {
        // 无窗模式：仅解码并计数（帧在 on_message 的 rd 回调线程中处理）
        PLOG_INFO << "rd viewer: headless mode, decoding without display";
        while (running_)
            Sleep(200);
    }

    Gdiplus::GdiplusShutdown(gdiplus_token);
}

LRESULT CALLBACK RdViewer::wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    RdViewer *self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto *cs = reinterpret_cast<CREATESTRUCT *>(lparam);
        self = static_cast<RdViewer *>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<RdViewer *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->handle_msg(msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT RdViewer::handle_msg(UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);
        RECT rc;
        GetClientRect(hwnd_, &rc);
        paint(hdc, rc);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1; // 自绘，避免闪烁

    case WM_MOUSEMOVE:
        send_mouse_move(static_cast<short>(LOWORD(lparam)),
                        static_cast<short>(HIWORD(lparam)));
        return 0;
    case WM_LBUTTONDOWN:
        send_button(0x01, true);
        return 0;
    case WM_LBUTTONUP:
        send_button(0x01, false);
        return 0;
    case WM_RBUTTONDOWN:
        send_button(0x02, true);
        return 0;
    case WM_RBUTTONUP:
        send_button(0x02, false);
        return 0;
    case WM_MBUTTONDOWN:
        send_button(0x04, true);
        return 0;
    case WM_MBUTTONUP:
        send_button(0x04, false);
        return 0;
    case WM_MOUSEWHEEL:
        send_wheel(static_cast<short>(HIWORD(wparam)));
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (!(lparam & 0x40000000))
        { // 过滤自动重复
            send_key(static_cast<uint8_t>(wparam), true);
        }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        send_key(static_cast<uint8_t>(wparam), false);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
    {
        auto m = rd::make_msg(rd::Stop, nullptr, 0);
        if (send_)
            send_(m.data(), m.size());
        running_ = false;
        PostQuitMessage(0);
        return 0;
    }
    default:
        return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

void RdViewer::paint(HDC hdc, const RECT &rc)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);

    if (!bgra_.empty() && fw_ > 0 && fh_ > 0)
    {
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = fw_;
        bmi.bmiHeader.biHeight = -fh_; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(hdc, HALFTONE);
        SetBrushOrgEx(hdc, 0, 0, nullptr);
        StretchDIBits(hdc, 0, 0, rc.right, rc.bottom,
                      0, 0, fw_, fh_, bgra_.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    }
    else
    {
        FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
        std::string s = status_.empty() ? "waiting for remote frames..." : status_;
        SetBkMode(hdc, TRANSPARENT);
        RECT trc = rc;
        DrawTextA(hdc, s.c_str(), -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

void RdViewer::on_message(const uint8_t *data, size_t len)
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
    case rd::Frame:
    {
        if (plen < 4)
            return;
        if (decode_jpeg(p + 4, plen - 4))
        {
            if (!sized_ && hwnd_)
            {
                sized_ = true;
                resize_to_frame();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            frames_++;
            if (frames_ == 1 || frames_ % 100 == 0)
            {
                PLOG_DEBUG << "rd viewer: frame #" << frames_
                           << " " << fw_ << "x" << fh_;
            }
        }
        break;
    }
    case rd::FrameH264:
    {
        if (plen < 5)
            return;
        int w = rd::get16(p);
        int h = rd::get16(p + 2);
        (void)w;
        (void)h;

        std::vector<uint8_t> bgra;
        int dw = 0, dh = 0;
        if (!h264_.is_open())
            h264_.open();
        if (!h264_.decode(p + 5, plen - 5, bgra, dw, dh))
            break;

        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            fw_ = dw;
            fh_ = dh;
            bgra_ = std::move(bgra);
        }

        frames_++;
        frames_decoded_ = frames_;
        if (!sized_ && hwnd_)
        {
            sized_ = true;
            resize_to_frame();
        }
        if (hwnd_)
            InvalidateRect(hwnd_, nullptr, FALSE);

        if (frames_ == 1 || frames_ % 100 == 0)
        {
            PLOG_INFO << "rd viewer: frame #" << frames_ << " decoded, "
                      << dw << "x" << dh;
        }
        break;
    }
    case rd::Started:
        PLOG_INFO << "rd viewer: remote stream started";
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            status_.clear();
        }
        if (hwnd_)
            InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case rd::Stopped:
        PLOG_INFO << "rd viewer: remote stream stopped";
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            bgra_.clear();
            status_ = "remote stream stopped";
        }
        if (hwnd_)
            InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    case rd::Error:
    {
        uint8_t code = plen >= 1 ? p[0] : 0;
        PLOG_ERROR << "rd viewer: remote error code=" << (int)code;
        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            bgra_.clear();
            status_ = code == rd::ErrDisabled
                          ? "remote desktop is disabled on the exit side (start rtcsocks-exit with --rd)"
                          : "remote desktop error (code " + std::to_string(code) + ")";
        }
        if (hwnd_)
            InvalidateRect(hwnd_, nullptr, FALSE);
        break;
    }
    default:
        break;
    }
}

bool RdViewer::decode_jpeg(const uint8_t *jpeg, size_t len)
{
    if (!jpeg || len == 0)
        return false;

    IStream *stream = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) != S_OK)
        return false;

    bool ok = false;
    do
    {
        if (stream->Write(jpeg, static_cast<ULONG>(len), nullptr) != S_OK)
            break;
        LARGE_INTEGER zero{};
        if (stream->Seek(zero, STREAM_SEEK_SET, nullptr) != S_OK)
            break;

        Gdiplus::Bitmap bmp(stream);
        if (bmp.GetLastStatus() != Gdiplus::Ok)
            break;

        Gdiplus::BitmapData bd{};
        Gdiplus::Rect rect(0, 0, bmp.GetWidth(), bmp.GetHeight());
        if (bmp.LockBits(&rect, Gdiplus::ImageLockModeRead,
                         PixelFormat32bppRGB, &bd) != Gdiplus::Ok)
            break;

        {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            fw_ = static_cast<int>(bmp.GetWidth());
            fh_ = static_cast<int>(bmp.GetHeight());
            bgra_.resize(static_cast<size_t>(fw_) * fh_ * 4);
            for (UINT y = 0; y < bd.Height; y++)
            {
                std::memcpy(&bgra_[static_cast<size_t>(y) * fw_ * 4],
                            static_cast<const uint8_t *>(bd.Scan0) + static_cast<size_t>(y) * bd.Stride,
                            static_cast<size_t>(fw_) * 4);
            }
        }
        bmp.UnlockBits(&bd);
        ok = true;
    } while (false);

    stream->Release();
    return ok;
}

void RdViewer::resize_to_frame()
{
    // 首帧到达时把窗口客户区适配到帧分辨率（不超过屏幕 70%）
    int cw = fw_;
    int ch = fh_;
    const int scr_w = GetSystemMetrics(SM_CXSCREEN);
    const int scr_h = GetSystemMetrics(SM_CYSCREEN);
    if (cw > scr_w * 7 / 10 || ch > scr_h * 7 / 10)
    {
        double scale = (std::min)(scr_w * 0.7 / cw, scr_h * 0.7 / ch);
        cw = static_cast<int>(cw * scale);
        ch = static_cast<int>(ch * scale);
    }

    RECT r{0, 0, cw, ch};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

void RdViewer::send_mouse_move(int x, int y)
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (rc.right <= 0 || rc.bottom <= 0)
        return;

    uint8_t pl[4];
    rd::put16(pl, static_cast<uint16_t>(x * 65535 / rc.right));
    rd::put16(pl + 2, static_cast<uint16_t>(y * 65535 / rc.bottom));
    auto m = rd::make_msg(rd::MouseMove, pl, sizeof(pl));
    if (send_)
        send_(m.data(), m.size());
}

void RdViewer::send_button(uint8_t flags, bool down)
{
    uint8_t pl[2] = {flags, static_cast<uint8_t>(down ? 1 : 0)};
    auto m = rd::make_msg(rd::MouseButton, pl, sizeof(pl));
    if (send_)
        send_(m.data(), m.size());
}

void RdViewer::send_wheel(int delta)
{
    uint8_t pl[2];
    rd::put16(pl, static_cast<uint16_t>(static_cast<int16_t>(delta)));
    auto m = rd::make_msg(rd::MouseWheel, pl, sizeof(pl));
    if (send_)
        send_(m.data(), m.size());
}

void RdViewer::send_key(uint8_t vk, bool down)
{
    uint8_t pl[2] = {vk, static_cast<uint8_t>(down ? 1 : 0)};
    auto m = rd::make_msg(rd::Key, pl, sizeof(pl));
    if (send_)
        send_(m.data(), m.size());
}

#endif // _WIN32
