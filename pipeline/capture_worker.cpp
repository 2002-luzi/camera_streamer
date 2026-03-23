#include "pipeline/capture_worker.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <vector>

#include "capture/v4l2_device.hpp"
#include "common/logger.hpp"
#include "common/time_utils.hpp"

namespace camera_streamer {
namespace {

struct MappedRegion {
    void* start{MAP_FAILED};
    std::size_t length{0};
};

// CaptureWorker 里仍然保留一个本地 ioctl 重试辅助函数，原因是：
// 1. poll/mmap/QBUF/DQBUF/STREAMON 这些调用都和采集线程的状态机强相关；
// 2. 这里需要非常贴近 Linux 系统调用层表达状态转换；
// 3. EINTR 重试是典型的系统调用细节，放在本地能让采集主循环更干净。
int RetryIoctl(int fd, unsigned long request, void* arg) {
    int rc = 0;
    do {
        rc = ioctl(fd, request, arg);
    } while (rc == -1 && errno == EINTR);
    return rc;
}

bool ParsePixelFormatFourcc(const std::string& text, std::uint32_t& fourcc) {
    if (text.size() != 4) {
        return false;
    }

    fourcc = v4l2_fourcc(text[0], text[1], text[2], text[3]);
    return true;
}

}  // namespace

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
    V4L2Device device(config_.device);
    if (!device.Open()) {
        return;
    }

    v4l2_capability capability{};
    if (!device.QueryCapabilities(capability)) {
        Log(LogLevel::kError, "Failed to query V4L2 capabilities in real capture mode");
        return;
    }

    // 真实采集至少需要两类能力：
    // 1. VIDEO_CAPTURE: 这个节点能提供视频帧；
    // 2. STREAMING: 这个节点支持流式 buffer 队列模型。
    if ((capability.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
        Log(LogLevel::kError, "Device does not advertise V4L2_CAP_VIDEO_CAPTURE");
        return;
    }
    if ((capability.capabilities & V4L2_CAP_STREAMING) == 0) {
        Log(LogLevel::kError, "Device does not advertise V4L2_CAP_STREAMING");
        return;
    }

    std::uint32_t pixel_format = 0;
    if (!ParsePixelFormatFourcc(config_.pixel_format, pixel_format)) {
        Log(LogLevel::kError, "CaptureConfig.pixel_format must be a 4-character FOURCC string");
        return;
    }

    CaptureFormat applied_format{};
    if (!device.SetCaptureFormat(config_.width, config_.height, pixel_format, applied_format)) {
        return;
    }

    CaptureFrameRate applied_rate{};
    if (!device.SetFrameRate(config_.fps, applied_rate)) {
        return;
    }

    std::vector<MmapBufferInfo> buffer_infos;
    if (!device.RequestMmapBuffers(config_.buffer_count, buffer_infos)) {
        return;
    }

    std::vector<MappedRegion> mapped_regions(buffer_infos.size());
    bool stream_on = false;

    auto cleanup = [&]() {
        if (stream_on) {
            // STREAMOFF 会让驱动停止采集，并把队列状态从 streaming 拉回 idle。
            // 这一步应该先于 munmap / REQBUFS(count=0)，否则驱动仍可能持有 buffer。
            v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (RetryIoctl(device.fd(), VIDIOC_STREAMOFF, &type) != 0) {
                std::ostringstream oss;
                oss << "VIDIOC_STREAMOFF failed for " << config_.device << ": "
                    << std::strerror(errno);
                Log(LogLevel::kWarn, oss.str());
            }
        }

        for (auto& region : mapped_regions) {
            if (region.start != MAP_FAILED) {
                if (munmap(region.start, region.length) != 0) {
                    std::ostringstream oss;
                    oss << "munmap failed for " << config_.device << ": "
                        << std::strerror(errno);
                    Log(LogLevel::kWarn, oss.str());
                }
                region.start = MAP_FAILED;
                region.length = 0;
            }
        }

        if (!buffer_infos.empty()) {
            device.ReleaseMmapBuffers();
        }
    };

    // 根据 QUERYBUF 返回的 offset/length 执行真正的 mmap。
    // 到这一步，用户态才真正“看见”驱动 buffer 对应的内存区域。
    for (std::size_t i = 0; i < buffer_infos.size(); ++i) {
        const auto& info = buffer_infos[i];
        void* region = mmap(nullptr, info.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                            device.fd(), static_cast<off_t>(info.offset));
        if (region == MAP_FAILED) {
            std::ostringstream oss;
            oss << "mmap failed for buffer index " << info.index << " on " << config_.device
                << ": " << std::strerror(errno);
            Log(LogLevel::kError, oss.str());
            cleanup();
            return;
        }

        mapped_regions[i].start = region;
        mapped_regions[i].length = info.length;
    }

    // 在 STREAMON 之前，需要先把所有空 buffer 通过 QBUF 交还给驱动。
    // 驱动拿到这些 buffer 后，才能开始 DMA / CSI 填帧。
    for (const auto& info : buffer_infos) {
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = info.index;

        if (RetryIoctl(device.fd(), VIDIOC_QBUF, &buffer) != 0) {
            std::ostringstream oss;
            oss << "VIDIOC_QBUF failed for index " << info.index << " on " << config_.device
                << ": " << std::strerror(errno);
            Log(LogLevel::kError, oss.str());
            cleanup();
            return;
        }
    }

    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (RetryIoctl(device.fd(), VIDIOC_STREAMON, &type) != 0) {
        std::ostringstream oss;
        oss << "VIDIOC_STREAMON failed for " << config_.device << ": " << std::strerror(errno);
        Log(LogLevel::kError, oss.str());
        cleanup();
        return;
    }
    stream_on = true;

    {
        std::ostringstream oss;
        oss << "Real capture started on " << config_.device
            << " format=" << V4L2Device::FourccToString(applied_format.pixel_format)
            << " size=" << applied_format.width << 'x' << applied_format.height
            << " fps=";
        if (applied_rate.numerator != 0) {
            oss << applied_rate.denominator << '/' << applied_rate.numerator;
        } else {
            oss << "unknown";
        }
        oss << " buffers=" << buffer_infos.size();
        Log(LogLevel::kInfo, oss.str());
    }

    while (running_) {
        pollfd descriptor{};
        descriptor.fd = device.fd();
        descriptor.events = POLLIN | POLLPRI;

        const int poll_rc = poll(&descriptor, 1, static_cast<int>(config_.poll_timeout_ms));
        if (poll_rc == 0) {
            // poll 超时并不一定是错误；在第一阶段里更适合直接继续等待。
            continue;
        }
        if (poll_rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::ostringstream oss;
            oss << "poll failed for " << config_.device << ": " << std::strerror(errno);
            Log(LogLevel::kError, oss.str());
            break;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            std::ostringstream oss;
            oss << "poll reported error events 0x" << std::hex << descriptor.revents << std::dec
                << " on " << config_.device;
            Log(LogLevel::kError, oss.str());
            break;
        }
        if ((descriptor.revents & (POLLIN | POLLPRI)) == 0) {
            continue;
        }

        // DQBUF 代表：驱动把一帧已经填好的 buffer 返还给用户态。
        // 从这一刻开始，用户态可以读取这个 buffer 中的原始图像数据。
        v4l2_buffer buffer{};
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;

        if (RetryIoctl(device.fd(), VIDIOC_DQBUF, &buffer) != 0) {
            if (errno == EAGAIN) {
                continue;
            }
            std::ostringstream oss;
            oss << "VIDIOC_DQBUF failed for " << config_.device << ": " << std::strerror(errno);
            Log(LogLevel::kError, oss.str());
            break;
        }

        if (buffer.index >= mapped_regions.size()) {
            Log(LogLevel::kError, "Driver returned an out-of-range buffer index in VIDIOC_DQBUF");
            break;
        }

        const auto& region = mapped_regions[buffer.index];

        // 第一阶段明确选择“先拷贝再回队列”。
        // 这样可以：
        // 1. 尽快把驱动 buffer 交还给内核；
        // 2. 让后续编码线程只处理用户态自己的 Frame；
        // 3. 避免一开始就把零拷贝/生命周期管理复杂化。
        Frame frame;
        frame.sequence = buffer.sequence;
        frame.capture_time_ms = NowSteadyMs();
        frame.width = applied_format.width;
        frame.height = applied_format.height;
        frame.pixel_format = V4L2Device::FourccToString(applied_format.pixel_format);

        std::size_t bytes_to_copy = buffer.bytesused;
        if (bytes_to_copy == 0) {
            // 某些驱动在原始采集路径里 bytesused 可能不给出明确值。
            // 这时退回到当前格式协商结果里的 size_image 会更稳妥。
            bytes_to_copy = applied_format.size_image;
        }
        bytes_to_copy = std::min(bytes_to_copy, region.length);

        frame.data.resize(bytes_to_copy);
        std::memcpy(frame.data.data(), region.start, bytes_to_copy);

        // 拷贝完成后立即 QBUF 回驱动，让内核尽快继续填下一帧。
        v4l2_buffer requeue{};
        requeue.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        requeue.memory = V4L2_MEMORY_MMAP;
        requeue.index = buffer.index;

        if (RetryIoctl(device.fd(), VIDIOC_QBUF, &requeue) != 0) {
            std::ostringstream oss;
            oss << "VIDIOC_QBUF(requeue) failed for index " << buffer.index << " on "
                << config_.device << ": " << std::strerror(errno);
            Log(LogLevel::kError, oss.str());
            break;
        }

        if (!output_queue_.Push(std::move(frame))) {
            break;
        }
        ++metrics_.captured_frames;
    }

    cleanup();
}

}  // namespace camera_streamer
