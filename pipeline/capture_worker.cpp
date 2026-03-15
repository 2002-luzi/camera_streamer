#include "pipeline/capture_worker.hpp"

#include <chrono>
#include <sstream>
#include <thread>

#include "common/logger.hpp"
#include "common/time_utils.hpp"

namespace camera_streamer {

CaptureWorker::CaptureWorker(CaptureConfig config, BoundedQueue<Frame>& output_queue, PipelineMetrics& metrics)
    : config_(std::move(config)), output_queue_(output_queue), metrics_(metrics) {}

CaptureWorker::~CaptureWorker() {
    Stop();
}

void CaptureWorker::Start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&CaptureWorker::Run, this);
}

void CaptureWorker::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void CaptureWorker::Run() {
    if (config_.simulate) {
        RunSimulatedLoop();
    } else {
        RunRealLoop();
    }
}

void CaptureWorker::RunSimulatedLoop() {
    const auto frame_period = std::chrono::milliseconds(
        config_.fps == 0 ? 100 : static_cast<int>(1000 / config_.fps));

    std::uint64_t sequence = 0;
    while (running_) {
        Frame frame;
        frame.sequence = sequence++;
        frame.capture_time_ms = NowSteadyMs();
        frame.width = config_.width;
        frame.height = config_.height;
        frame.pixel_format = "SIMU";
        frame.data.resize(frame.width * frame.height);
        std::fill(frame.data.begin(), frame.data.end(),
                  static_cast<std::uint8_t>(frame.sequence & 0xFF));

        if (!output_queue_.Push(std::move(frame))) {
            break;
        }
        ++metrics_.captured_frames;
        std::this_thread::sleep_for(frame_period);
    }
}

void CaptureWorker::RunRealLoop() {
    std::ostringstream oss;
    oss << "Real V4L2 capture is not wired yet. Target device: " << config_.device;
    Log(LogLevel::kWarn, oss.str());

    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

}  // namespace camera_streamer

