#pragma once

#include "capture/frame.hpp"
#include "encode/encoded_packet.hpp"

namespace camera_streamer {

class Encoder {
public:
    virtual ~Encoder() = default;
    virtual EncodedPacket Encode(const Frame& frame) = 0;
};

class MjpegEncoderStub : public Encoder {
public:
    EncodedPacket Encode(const Frame& frame) override;
};

}  // namespace camera_streamer

