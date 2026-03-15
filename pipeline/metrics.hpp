#pragma once

#include <atomic>

namespace camera_streamer {

struct PipelineMetrics {
    std::atomic<unsigned long long> captured_frames{0};
    std::atomic<unsigned long long> encoded_frames{0};
    std::atomic<unsigned long long> published_packets{0};
    std::atomic<unsigned long long> dropped_frames{0};
};

}  // namespace camera_streamer

