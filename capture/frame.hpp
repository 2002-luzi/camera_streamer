#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace camera_streamer {

struct Frame {
    std::uint64_t sequence{0};
    std::int64_t capture_time_ms{0};
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::string pixel_format;
    std::vector<std::uint8_t> data;
};

}  // namespace camera_streamer

