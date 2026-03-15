#pragma once

#include <atomic>
#include <thread>

#include "capture/frame.hpp"
#include "common/bounded_queue.hpp"
#include "encode/encoded_packet.hpp"
#include "encode/encoder.hpp"
#include "pipeline/metrics.hpp"

namespace camera_streamer {

class EncodeWorker {
public:
    EncodeWorker(BoundedQueue<Frame>& input_queue,
                 BoundedQueue<EncodedPacket>& output_queue,
                 Encoder& encoder,
                 PipelineMetrics& metrics);
    ~EncodeWorker();

    void Start();
    void Stop();

private:
    void Run();

    BoundedQueue<Frame>& input_queue_;
    BoundedQueue<EncodedPacket>& output_queue_;
    Encoder& encoder_;
    PipelineMetrics& metrics_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace camera_streamer

