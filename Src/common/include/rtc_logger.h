#pragma once
#include <atomic>
#include <array>
#include <thread>
#include <fstream>
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <condition_variable>
#include <mutex>
#include <rtc/rtc.hpp>
#include "concurrentqueue.h"
// ---------------- 日志级别转字符串 ----------------
inline const char* logLevelToString(rtc::LogLevel level) {
    switch (level) {
        case rtc::LogLevel::None:    return "NONE ";
        case rtc::LogLevel::Fatal:   return "FATAL";
        case rtc::LogLevel::Error:   return "ERROR";
        case rtc::LogLevel::Warning: return "WARN ";
        case rtc::LogLevel::Info:    return "INFO ";
        case rtc::LogLevel::Debug:   return "DEBUG";
        case rtc::LogLevel::Verbose: return "VERB ";
        default:                     return "?????";
    }
}

#include <filesystem>
#include <string>

#ifdef _WIN32
    #include <windows.h>
#elif __APPLE__
    #include <mach-o/dyld.h>
#else // Linux
    #include <unistd.h>
    #include <limits.h>
#endif

namespace fs = std::filesystem;

fs::path getExecutableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();

#elif __APPLE__
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        // buf 太小，需要更大的缓冲区（极少发生）
        std::vector<char> bigBuf(size);
        _NSGetExecutablePath(bigBuf.data(), &size);
        return fs::canonical(fs::path(bigBuf.data())).parent_path();
    }
    return fs::canonical(fs::path(buf)).parent_path();

#else // Linux
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        return fs::path(buf).parent_path();
    }
    return fs::current_path(); // 兜底方案

#endif
}

class RtcLogger {
public:
    static RtcLogger& instance() {
        static RtcLogger inst;
        return inst;
    }

    void init(const std::string& filepath, bool console = false) {
        console_ = console;
        auto exec_dir = getExecutableDir();
        std::string log_path = exec_dir.string() + "/" + filepath;
        //file_.open(log_path, std::ios::out | std::ios::app);
        file_.open(log_path);
        running_ = true;
        worker_ = std::thread(&RtcLogger::workerLoop, this);
    }

    void shutdown() {
        if (!running_) return;
        running_ = false;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        drain();
        file_.flush();
        file_.close();
    }

    // 热路径：只打时间戳（数值），不做字符串格式化，格式化留给后台线程
    void log(rtc::LogLevel level, std::string message) {
        Entry e;
        e.time = std::chrono::system_clock::now();
        e.level = level;
        e.message = std::move(message);
        if (!queue_.enqueue(std::move(e))) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        cv_.notify_one();
    }

    ~RtcLogger() { shutdown(); }

private:
    struct Entry {
        std::chrono::system_clock::time_point time;
        rtc::LogLevel level;
        std::string message;
    };

    RtcLogger() = default;

    void workerLoop() {
        Entry e;
        while (running_) {
            if (queue_.try_dequeue(e)) {
                writeEntry(e);
            } else {
                std::unique_lock<std::mutex> lk(cv_mutex_);
                cv_.wait_for(lk, std::chrono::milliseconds(50));
            }
        }
    }

    void drain() {
        Entry e;
        while (queue_.try_dequeue(e)) writeEntry(e);
    }

    // 格式化在后台线程做，不影响调用者
    void writeEntry(const Entry& e) {
        auto t = std::chrono::system_clock::to_time_t(e.time);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      e.time.time_since_epoch()) % 1000;

        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count()
            << " [" << logLevelToString(e.level) << "] "
            << e.message;

        const std::string line = oss.str();
        file_ << line << '\n';
        file_.flush();
        if (console_)
        {
            std::cout << line << '\n';
            std::cout.flush();
        }
    }

    moodycamel::ConcurrentQueue<Entry> queue_;
    std::ofstream file_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> dropped_{0};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    bool console_{false};
};

// 兼容原回调签名
void rtcLogCallback(rtc::LogLevel level, std::string message) {
    RtcLogger::instance().log(level, std::move(message));
}