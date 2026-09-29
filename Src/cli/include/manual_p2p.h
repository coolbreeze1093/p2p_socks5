// manual_p2p.h —— 无信令的 P2P 数据通道（rtcsocks 命令行版专用）
//
// 与旧 client/server 的 WebSocket 信令不同，这里双方通过命令行"手动"交换
// WebRTC SDP（base64 单行文本）完成连接：
//   1. 客户端(offerer)     createOffer()  -> 打印 OFFER
//   2. 出口端(answerer)    acceptOffer()  <- 粘贴 OFFER -> 打印 ANSWER
//   3. 客户端(offerer)     acceptAnswer() <- 粘贴 ANSWER -> P2P 建立
// 不依赖任何信令服务器；SDP 中已包含全部候选地址（非 trickle，收集完成后取出）。
#pragma once
#include <rtc/rtc.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>

class ManualP2P
{
public:
    using BinaryCallback = std::function<void(const uint8_t *data, size_t len)>;
    using StateCallback = std::function<void()>;

    // stun 等 ICE 配置，需在 createOffer/acceptOffer 之前调用
    void init(const rtc::Configuration &config);

    // ---------- offerer（本地 SOCKS5 端）----------
    // 创建 PeerConnection + DataChannel，等待候选收集完成，
    // 返回 base64 编码的 OFFER SDP；失败(超时)返回空串。
    std::string createOffer(int timeout_sec = 30);

    // 设置对端返回的 base64 ANSWER，成功返回 true
    bool acceptAnswer(const std::string &answer_b64);

    // ---------- answerer（出口端）----------
    // 设置对端的 base64 OFFER，等待候选收集完成，返回 base64 编码的 ANSWER；
    // DataChannel 由 onDataChannel 回调接入；失败(超时/非法)返回空串
    std::string acceptOffer(const std::string &offer_b64, int timeout_sec = 30);

    void send(const uint8_t *data, size_t len);
    // 远程桌面通道发送（通道未就绪时打日志丢弃）
    void sendRd(const uint8_t *data, size_t len);
    void close();

    // P2P 数据通道(label="data")收到的二进制数据
    void onData(BinaryCallback cb) { data_callback_ = std::move(cb); }
    // 任一 DataChannel 打开，可以开始传输（只通知一次）
    void onConnected(StateCallback cb) { connected_callback_ = std::move(cb); }
    // 连接失败或关闭
    void onClosed(StateCallback cb) { closed_callback_ = std::move(cb); }

    // ---------- base64 工具（供 SDP 交换使用）----------
    static std::string base64Encode(const std::string &raw);
    // 解码失败抛 std::runtime_error；容忍中间空白
    static std::string base64Decode(const std::string &b64);

private:
    std::string waitLocalSdp(int timeout_sec);
    void bindPeerConnection();
    void bindDataChannel(std::shared_ptr<rtc::DataChannel> dc);

    rtc::Configuration config_;
    std::shared_ptr<rtc::PeerConnection> pc_;
    std::shared_ptr<rtc::DataChannel> dc_;

    BinaryCallback data_callback_;
    BinaryCallback rd_data_callback_;
    StateCallback connected_callback_;
    StateCallback closed_callback_;
    std::atomic<bool> connected_notified_{false};
};

// ---------- SDP 交换辅助（命令行/文件）----------

// 从粘贴文本中提取 SDP 的 base64：
// 自动跳过 -----BEGIN/END----- 标记行与空白行；
// 先尝试逐行解码，再尝试多行拼接；找不到有效 SDP 返回空串
std::string ExtractSdp(const std::string &text);

// 阻塞从 stdin 读取，直到出现有效 SDP 或 EOF/END 标记；失败返回空串
std::string ReadSdpFromStdin(const std::string &what);

// 轮询读取文件中出现的有效 SDP，超时返回空串
std::string ReadSdpFromFile(const std::string &path, int timeout_sec);

// 覆盖写入 SDP 文件，失败返回 false
bool WriteSdpToFile(const std::string &path, const std::string &sdp_b64);

// 按 BEGIN/END 标记块打印到控制台
void PrintSdp(const char *title, const std::string &sdp_b64);
