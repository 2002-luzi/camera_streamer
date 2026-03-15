#include "publish/rtsp_publisher.hpp"

#include <sstream>

#include "common/logger.hpp"

namespace camera_streamer {

RtspPublisher::RtspPublisher(std::string url) : url_(std::move(url)) {}

bool RtspPublisher::Open() {
    opened_ = true;
    Log(LogLevel::kInfo, "RTSP publisher stub opened for URL: " + url_);
    return true;
}

void RtspPublisher::Close() {
    opened_ = false;
}

bool RtspPublisher::Publish(const EncodedPacket& packet) {
    if (!opened_) {
        return false;
    }

    std::ostringstream oss;
    oss << "Stub publish seq=" << packet.sequence
        << " codec=" << packet.codec
        << " bytes=" << packet.payload.size();
    Log(LogLevel::kDebug, oss.str());
    return true;
}

}  // namespace camera_streamer

