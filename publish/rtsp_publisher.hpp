#pragma once

#include <string>

#include "encode/encoded_packet.hpp"

namespace camera_streamer {

class RtspPublisher {
public:
    explicit RtspPublisher(std::string url);

    bool Open();
    void Close();
    bool Publish(const EncodedPacket& packet);

private:
    std::string url_;
    bool opened_{false};
};

}  // namespace camera_streamer

