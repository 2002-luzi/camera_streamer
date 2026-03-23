#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace camera_streamer {

struct CaptureConfig {
    std::string device{"/dev/video1"};
    std::uint32_t width{640};
    std::uint32_t height{480};
    std::string pixel_format{"YUYV"};
    std::uint32_t fps{10};
    std::uint32_t buffer_count{4};
    std::uint32_t poll_timeout_ms{1000};
    bool simulate{true};
};

struct PipelineConfig {
    CaptureConfig capture;
    std::size_t queue_depth{4};
    std::string rtsp_url{"rtsp://127.0.0.1:8554/imx6ull"};
};

}  // namespace camera_streamer
