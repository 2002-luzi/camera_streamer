#pragma once

#include <atomic>
#include <thread>

#include "pipeline/metrics.hpp"

namespace camera_streamer {

class StatsWorker {
public:
    explicit StatsWorker(PipelineMetrics& metrics);
    ~StatsWorker();

    void Start();
    void Stop();

private:
    void Run();

    PipelineMetrics& metrics_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace camera_streamer

