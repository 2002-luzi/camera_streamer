#include "pipeline/publish_worker.hpp"

namespace camera_streamer {

PublishWorker::PublishWorker(BoundedQueue<EncodedPacket>& input_queue,
                             RtspPublisher& publisher,
                             PipelineMetrics& metrics)
    : input_queue_(input_queue), publisher_(publisher), metrics_(metrics) {}

PublishWorker::~PublishWorker() {
    Stop();
}

void PublishWorker::Start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&PublishWorker::Run, this);
}

void PublishWorker::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void PublishWorker::Run() {
    while (running_) {
        auto packet = input_queue_.Pop();
        if (!packet.has_value()) {
            break;
        }
        if (publisher_.Publish(packet.value())) {
            ++metrics_.published_packets;
        }
    }
}

}  // namespace camera_streamer

