#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "capture/v4l2_device.hpp"
#include "common/bounded_queue.hpp"
#include "common/logger.hpp"
#include "encode/encoded_packet.hpp"
#include "encode/encoder.hpp"
#include "pipeline/capture_worker.hpp"
#include "pipeline/encode_worker.hpp"
#include "pipeline/metrics.hpp"
#include "pipeline/pipeline_config.hpp"
#include "pipeline/publish_worker.hpp"
#include "pipeline/stats_worker.hpp"
#include "publish/rtsp_publisher.hpp"

namespace camera_streamer {
namespace {

std::atomic<bool> g_keep_running{true};

void HandleSignal(int /*signum*/) {
    g_keep_running = false;
}

void PrintUsage(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program << "                # run pipeline skeleton in simulation mode\n"
              << "  " << program << " --probe DEVICE # print V4L2 capability summary\n"
              << "  " << program << " --set-format DEVICE WIDTH HEIGHT FOURCC\n"
              << "                             # apply capture format and print driver result\n"
              << "  " << program << " --real         # disable simulation mode\n";
}

// 把命令行里的宽高参数解析成 uint32_t。
// 这里故意保持成一个很小的辅助函数，后面如果继续加
// --reqbufs / --dump-frames 之类的调试命令，可以直接复用。
bool ParseUnsigned(const std::string& text, std::uint32_t& value) {
    try {
        const auto parsed = std::stoul(text);
        value = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// FOURCC 必须是 4 个字符，例如 YUYV / UYVY / MJPG。
// 命令行直接收字符串，比让用户自己输入十六进制数更直观，
// 也更适合当前以“验证驱动格式协商”为目标的小工具用法。
bool ParseFourcc(const std::string& text, std::uint32_t& fourcc) {
    if (text.size() != 4) {
        return false;
    }

    fourcc = v4l2_fourcc(text[0], text[1], text[2], text[3]);
    return true;
}

// 把驱动最终接受的格式打印出来。
// 之所以集中成一个函数，而不是散落在 main 里逐字段输出，
// 是为了后面可以很方便地在其他命令路径中复用相同格式。
void PrintCaptureFormat(std::ostream& os, const CaptureFormat& format) {
    os << "Applied format:\n"
       << "  - width: " << format.width << '\n'
       << "  - height: " << format.height << '\n'
       << "  - pixel_format: " << V4L2Device::FourccToString(format.pixel_format) << '\n'
       << "  - bytes_per_line: " << format.bytes_per_line << '\n'
       << "  - size_image: " << format.size_image << '\n';
}

}  // namespace
}  // namespace camera_streamer

int main(int argc, char** argv) {
    using namespace camera_streamer;

    PipelineConfig config;

    if (argc >= 2) {
        const std::string arg1 = argv[1];
        if (arg1 == "--probe") {
            if (argc < 3) {
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }
            V4L2Device device(argv[2]);
            if (!device.Open()) {
                return EXIT_FAILURE;
            }
            device.DumpInfo(std::cout);
            return EXIT_SUCCESS;
        }
        if (arg1 == "--set-format") {
            // 这个命令的目标非常单一：
            // 1. 打开指定视频节点；
            // 2. 调用 VIDIOC_S_FMT 发起格式协商；
            // 3. 把驱动最终接受的结果打印出来。
            // 这样我们就能在不引入 mmap / queue / streamon 的前提下，
            // 先验证“设备格式配置”这一步已经打通。
            if (argc < 6) {
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }

            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::uint32_t pixel_format = 0;
            if (!ParseUnsigned(argv[3], width) || !ParseUnsigned(argv[4], height) ||
                !ParseFourcc(argv[5], pixel_format)) {
                std::cerr << "Invalid width, height, or FOURCC.\n";
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }

            // 这里复用 V4L2Device 统一管理设备打开和 ioctl 细节，
            // 保持 main 只负责参数解析和结果展示。
            V4L2Device device(argv[2]);
            if (!device.Open()) {
                return EXIT_FAILURE;
            }

            CaptureFormat applied_format{};
            if (!device.SetCaptureFormat(width, height, pixel_format, applied_format)) {
                return EXIT_FAILURE;
            }

            PrintCaptureFormat(std::cout, applied_format);
            return EXIT_SUCCESS;
        }
        if (arg1 == "--real") {
            config.capture.simulate = false;
        } else if (arg1 == "--help" || arg1 == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        }
    }

    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);

    Log(LogLevel::kInfo, "Starting camera_streamer skeleton");
    if (config.capture.simulate) {
        Log(LogLevel::kInfo, "Simulation mode is enabled");
    }

    BoundedQueue<Frame> raw_queue(config.queue_depth);
    BoundedQueue<EncodedPacket> encoded_queue(config.queue_depth);
    PipelineMetrics metrics;

    MjpegEncoderStub encoder;
    RtspPublisher publisher(config.rtsp_url);
    if (!publisher.Open()) {
        Log(LogLevel::kError, "Failed to open RTSP publisher");
        return EXIT_FAILURE;
    }

    CaptureWorker capture_worker(config.capture, raw_queue, metrics);
    EncodeWorker encode_worker(raw_queue, encoded_queue, encoder, metrics);
    PublishWorker publish_worker(encoded_queue, publisher, metrics);
    StatsWorker stats_worker(metrics);

    capture_worker.Start();
    encode_worker.Start();
    publish_worker.Start();
    stats_worker.Start();

    while (g_keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    Log(LogLevel::kInfo, "Stopping camera_streamer skeleton");
    raw_queue.Close();
    encoded_queue.Close();
    capture_worker.Stop();
    encode_worker.Stop();
    publish_worker.Stop();
    stats_worker.Stop();
    publisher.Close();
    return EXIT_SUCCESS;
}
