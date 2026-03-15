#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace camera_streamer {

enum class LogLevel {
    kInfo,
    kWarn,
    kError,
    kDebug,
};

inline const char* ToString(LogLevel level) {
    switch (level) {
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kDebug:
            return "DEBUG";
    }
    return "UNKNOWN";
}

inline std::mutex& LogMutex() {
    static std::mutex mutex;
    return mutex;
}

inline void Log(LogLevel level, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now{};
    localtime_r(&now_time, &tm_now);

    std::lock_guard<std::mutex> lock(LogMutex());
    std::cerr << std::put_time(&tm_now, "%F %T")
              << " [" << ToString(level) << "]"
              << " [tid " << std::this_thread::get_id() << "] "
              << message << '\n';
}

}  // namespace camera_streamer

