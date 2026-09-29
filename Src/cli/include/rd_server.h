// rd_server.h —— 远程桌面被控端（rtcsocks-exit，仅 Windows）
//
// 收到控制端的 Start 后启动抓屏线程：GDI 抓屏 -> GDI+ JPEG 压缩 ->
// 经 rd 通道发送；画面未变化时跳过发送。同时把控制端回传的鼠标键盘
// 消息用 SendInput 注入本机。
#pragma once
#ifdef _WIN32

#include "rd_codec.h"
#include "rd_protocol.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

class RdServer
{
public:
    // 通过 rd DataChannel 发送（由 main 绑定到 ManualP2P::sendRd）
    using SendFunc = std::function<void(const uint8_t *data, size_t len)>;

    RdServer() = default;
    ~RdServer();

    RdServer(const RdServer &) = delete;
    RdServer &operator=(const RdServer &) = delete;

    void set_send(SendFunc f) { send_ = std::move(f); }

    // 处理控制端发来的 rd 消息（Start/Stop/输入事件），内部解析协议帧
    void handle_input(const uint8_t *data, size_t len);

    // 停止抓屏线程并发送 Stopped 应答
    void stop();

private:
    void start_stream(uint8_t fps, uint8_t quality, uint16_t max_width, uint8_t codec);
    void capture_loop();
    bool encode_jpeg(const std::vector<uint8_t> &bgra, int w, int h,
                     uint8_t quality, std::vector<uint8_t> &out);
    void send_msg(const uint8_t *data, size_t len);
    void inject_mouse_move(const uint8_t *p, uint32_t plen);
    void inject_mouse_button(const uint8_t *p, uint32_t plen);
    void inject_mouse_wheel(const uint8_t *p, uint32_t plen);
    void inject_key(const uint8_t *p, uint32_t plen);

    SendFunc send_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    uint8_t fps_ = 8;
    uint8_t quality_ = 60;
    uint16_t max_width_ = 1280;
    uint8_t codec_ = rd::CodecAuto;

    // H.264（OpenH264），不可用时回退 JPEG
    rdcodec::H264Encoder encoder_;
    bool use_h264_ = false;
};

#endif // _WIN32
