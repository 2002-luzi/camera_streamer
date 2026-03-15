#pragma once

#include <atomic>
#include <thread>

#include "common/bounded_queue.hpp"
#include "encode/encoded_packet.hpp"
#include "pipeline/metrics.hpp"
#include "publish/rtsp_publisher.hpp"

namespace camera_streamer {

class PublishWorker {
public:
    PublishWorker(BoundedQueue<EncodedPacket>& input_queue,
                  RtspPublisher& publisher,
                  PipelineMetrics& metrics);
    ~PublishWorker();

    void Start();
    void Stop();

private:
    void Run();

    BoundedQueue<EncodedPacket>& input_queue_;
    RtspPublisher& publisher_;
    PipelineMetrics& metrics_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace camera_streamer

