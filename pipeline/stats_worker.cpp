#include "pipeline/stats_worker.hpp"

#include <chrono>
#include <sstream>
#include <thread>

#include "common/logger.hpp"

namespace camera_streamer {

StatsWorker::StatsWorker(PipelineMetrics& metrics) : metrics_(metrics) {}

StatsWorker::~StatsWorker() {
    Stop();
}

void StatsWorker::Start() {
    if (running_.exchange(true)) {
        return;
    }
    worker_ = std::thread(&StatsWorker::Run, this);
}

void StatsWorker::Stop() {
    if (!running_.exchange(false)) {
        return;
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void StatsWorker::Run() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::ostringstream oss;
        oss << "captured=" << metrics_.captured_frames.load()
            << " encoded=" << metrics_.encoded_frames.load()
            << " published=" << metrics_.published_packets.load()
            << " dropped=" << metrics_.dropped_frames.load();
        Log(LogLevel::kInfo, oss.str());
    }
}

}  // namespace camera_streamer

