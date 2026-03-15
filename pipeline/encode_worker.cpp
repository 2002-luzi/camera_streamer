#include "pipeline/encode_worker.hpp"

namespace camera_streamer {

EncodeWorker::EncodeWorker(BoundedQueue<Frame>& input_queue,
                           BoundedQueue<EncodedPacket>& output_queue,
                           Encoder& encoder,
                           PipelineMetrics& metrics)
    : input_queue_(input_queue),
      output_queue_(output_queue),
      encoder_(encoder),
      metrics_(metrics) {}

EncodeWorker::~EncodeWorker() {
    Stop();
}

void EncodeWorker::Start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&EncodeWorker::Run, this);
}

void EncodeWorker::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void EncodeWorker::Run() {
    while (running_) {
        auto frame = input_queue_.Pop();
        if (!frame.has_value()) {
            break;
        }
        auto packet = encoder_.Encode(frame.value());
        if (!output_queue_.Push(std::move(packet))) {
            break;
        }
        ++metrics_.encoded_frames;
    }
}

}  // namespace camera_streamer

