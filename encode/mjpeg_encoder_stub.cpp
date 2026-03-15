#include "encode/encoder.hpp"

#include "common/time_utils.hpp"

namespace camera_streamer {

EncodedPacket MjpegEncoderStub::Encode(const Frame& frame) {
    EncodedPacket packet;
    packet.sequence = frame.sequence;
    packet.capture_time_ms = frame.capture_time_ms;
    packet.encode_done_time_ms = NowSteadyMs();
    packet.codec = "mjpeg-stub";
    packet.keyframe = true;
    packet.payload = frame.data;
    return packet;
}

}  // namespace camera_streamer

