#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace camera_streamer {

struct EncodedPacket {
    std::uint64_t sequence{0};
    std::int64_t capture_time_ms{0};
    std::int64_t encode_done_time_ms{0};
    std::string codec;
    bool keyframe{true};
    std::vector<std::uint8_t> payload;
};

}  // namespace camera_streamer

