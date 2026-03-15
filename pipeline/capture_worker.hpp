#pragma once

#include <atomic>
#include <thread>

#include "capture/frame.hpp"
#include "common/bounded_queue.hpp"
#include "pipeline/metrics.hpp"
#include "pipeline/pipeline_config.hpp"

namespace camera_streamer {

class CaptureWorker {
public:
    CaptureWorker(CaptureConfig config, BoundedQueue<Frame>& output_queue, PipelineMetrics& metrics);
    ~CaptureWorker();

    void Start();
    void Stop();

private:
    void Run();
    void RunSimulatedLoop();
    void RunRealLoop();

    CaptureConfig config_;
    BoundedQueue<Frame>& output_queue_;
    PipelineMetrics& metrics_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace camera_streamer

