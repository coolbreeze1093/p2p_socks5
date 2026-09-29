// rd_viewer.h —— 远程桌面控制端（rtcsocks，仅 Windows）
//
// 独立线程跑 Win32 窗口：收到的 JPEG 帧解码为 BGRA 后 StretchDIBits 显示；
// 窗口内的鼠标/键盘事件按归一化坐标回传给被控端。窗口关闭即发送 Stop。
#pragma once
#ifdef _WIN32

#include "rd_codec.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class RdViewer
{
public:
    // 通过 rd DataChannel 发送（由 main 绑定到 ManualP2P::sendRd）
    using SendFunc = std::function<void(const uint8_t *data, size_t len)>;

    RdViewer() = default;
    ~RdViewer();

    RdViewer(const RdViewer &) = delete;
    RdViewer &operator=(const RdViewer &) = delete;

    // 启动窗口线程；连接建立后调用一次。
    // 窗口创建失败（如无交互桌面）时自动降级为无窗模式：仅解码计数，便于验证链路。
    bool start(SendFunc send, uint16_t fps, uint16_t quality, uint16_t max_width);

    // 停止窗口线程（窗口若还在则发送 Stop 并关闭）
    void stop();

    // 收到被控端的 rd 消息（Frame/FrameH264/Started/Stopped/Error），线程安全
    void on_message(const uint8_t *data, size_t len);

private:
    void run();
    static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT handle_msg(UINT msg, WPARAM wparam, LPARAM lparam);
    void paint(HDC hdc, const RECT &rc);
    bool decode_jpeg(const uint8_t *jpeg, size_t len);
    void resize_to_frame();
    void send_mouse_move(int x, int y);
    void send_button(uint8_t flags, bool down);
    void send_wheel(int delta);
    void send_key(uint8_t vk, bool down);

    SendFunc send_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> window_ok_{false};
    HWND hwnd_ = nullptr;

    uint16_t fps_ = 8;
    uint16_t quality_ = 60;
    uint16_t max_width_ = 1280;

    // 最新一帧（解码后 BGRA，top-down）；由 rd 回调线程写入、UI 线程读取
    std::mutex frame_mutex_;
    std::vector<uint8_t> bgra_;
    int fw_ = 0;
    int fh_ = 0;
    std::string status_; // 无画面时显示的提示文本
    bool sized_ = false;
    uint64_t frames_ = 0;
    uint64_t frames_decoded_ = 0;

    rdcodec::H264Decoder h264_;
};

#endif // _WIN32
