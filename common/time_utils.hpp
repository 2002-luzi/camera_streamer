#pragma once

#include <chrono>
#include <cstdint>

namespace camera_streamer {

inline std::int64_t NowSteadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace camera_streamer

