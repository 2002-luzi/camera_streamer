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
              << "  " << program << " --request-buffers DEVICE COUNT\n"
              << "                             # run REQBUFS + QUERYBUF and print mmap buffer metadata\n"
              << "  " << program << " --real [options]\n"
              << "                             # run real V4L2 capture mode\n"
              << "Options for --real:\n"
              << "    --device DEVICE          # default: /dev/video0\n"
              << "    --width WIDTH            # default: 640\n"
              << "    --height HEIGHT          # default: 480\n"
              << "    --pixel-format FOURCC    # default: YUYV\n"
              << "    --fps FPS                # default: 10\n"
              << "    --buffers COUNT          # default: 4\n"
              << "    --poll-timeout-ms MS     # default: 1000\n";
}

// 把命令行里的无符号整数参数解析成 uint32_t。
// 这里保持成一个小而通用的辅助函数，后面继续增加
// --stream-on / --dump-frames 之类的调试命令时可以直接复用。
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
// 也更适合当前以“分阶段验证 V4L2 ioctl 链路”为目标的小工具用法。
bool ParseFourcc(const std::string& text, std::uint32_t& fourcc) {
    if (text.size() != 4) {
        return false;
    }

    fourcc = v4l2_fourcc(text[0], text[1], text[2], text[3]);
    return true;
}

// 解析 --real 后面的常用调试参数。
// 这里故意只支持最核心的一小组配置，目的是让板端验证时不用每次改源码默认值。
bool ParseRealOptions(int argc, char** argv, PipelineConfig& config) {
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];

        auto require_value = [&](const char* name) -> bool {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << ".\n";
                return false;
            }
            return true;
        };

        if (arg == "--device") {
            if (!require_value("--device")) {
                return false;
            }
            config.capture.device = argv[++i];
            continue;
        }
        if (arg == "--width") {
            std::uint32_t value = 0;
            if (!require_value("--width") || !ParseUnsigned(argv[i + 1], value)) {
                std::cerr << "Invalid --width.\n";
                return false;
            }
            config.capture.width = value;
            ++i;
            continue;
        }
        if (arg == "--height") {
            std::uint32_t value = 0;
            if (!require_value("--height") || !ParseUnsigned(argv[i + 1], value)) {
                std::cerr << "Invalid --height.\n";
                return false;
            }
            config.capture.height = value;
            ++i;
            continue;
        }
        if (arg == "--pixel-format") {
            if (!require_value("--pixel-format") || std::string(argv[i + 1]).size() != 4) {
                std::cerr << "Invalid --pixel-format.\n";
                return false;
            }
            config.capture.pixel_format = argv[++i];
            continue;
        }
        if (arg == "--fps") {
            std::uint32_t value = 0;
            if (!require_value("--fps") || !ParseUnsigned(argv[i + 1], value) || value == 0) {
                std::cerr << "Invalid --fps.\n";
                return false;
            }
            config.capture.fps = value;
            ++i;
            continue;
        }
        if (arg == "--buffers") {
            std::uint32_t value = 0;
            if (!require_value("--buffers") || !ParseUnsigned(argv[i + 1], value) || value == 0) {
                std::cerr << "Invalid --buffers.\n";
                return false;
            }
            config.capture.buffer_count = value;
            ++i;
            continue;
        }
        if (arg == "--poll-timeout-ms") {
            std::uint32_t value = 0;
            if (!require_value("--poll-timeout-ms") || !ParseUnsigned(argv[i + 1], value) || value == 0) {
                std::cerr << "Invalid --poll-timeout-ms.\n";
                return false;
            }
            config.capture.poll_timeout_ms = value;
            ++i;
            continue;
        }

        std::cerr << "Unknown option for --real: " << arg << '\n';
        return false;
    }

    return true;
}

// 把驱动最终接受的格式打印出来。
// 之所以集中成一个函数，而不是散落在 main 里逐字段输出，
// 是为了后面可以很方便地在 probe / set-format / request-buffers
// 等多个命令路径中复用相同展示格式。
void PrintCaptureFormat(std::ostream& os, const CaptureFormat& format) {
    os << "Applied format:\n"
       << "  - width: " << format.width << '\n'
       << "  - height: " << format.height << '\n'
       << "  - pixel_format: " << V4L2Device::FourccToString(format.pixel_format) << '\n'
       << "  - bytes_per_line: " << format.bytes_per_line << '\n'
       << "  - size_image: " << format.size_image << '\n';
}

// 把 REQBUFS + QUERYBUF 得到的 mmap buffer 信息打印出来。
// 这一步非常适合作为“真实采集前”的独立验证命令，因为它能让我们确认：
// 1. 驱动是否真的为当前格式分配了 buffer；
// 2. 实际分配了多少个 buffer；
// 3. 后续 mmap() 所需的 length / offset 是多少。
void PrintMmapBuffers(std::ostream& os, const std::vector<MmapBufferInfo>& buffers) {
    os << "Mmap buffers (" << buffers.size() << "):\n";
    for (const auto& buffer : buffers) {
        os << "  - index: " << buffer.index << '\n'
           << "    length: " << buffer.length << '\n'
           << "    bytes_used: " << buffer.bytes_used << '\n'
           << "    offset: " << buffer.offset << '\n';
    }
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
        if (arg1 == "--request-buffers") {
            // 这个命令对应“真实 mmap 采集前的下一小步”：
            // 1. 先让驱动分配一组 MMAP buffer；
            // 2. 再逐个 QUERYBUF，拿到每个 buffer 的 length 和 offset；
            // 3. 为后续真正调用 mmap() 做准备。
            if (argc < 4) {
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }

            std::uint32_t count = 0;
            if (!ParseUnsigned(argv[3], count) || count == 0) {
                std::cerr << "Invalid COUNT.\n";
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }

            V4L2Device device(argv[2]);
            if (!device.Open()) {
                return EXIT_FAILURE;
            }

            // 打印当前格式有两个意义：
            // 1. 让 REQBUFS 的结果和当前格式关联起来看；
            // 2. 帮助确认 sizeimage / bytesperline 与 buffer length 是否合理。
            CaptureFormat current_format{};
            if (device.GetCaptureFormat(current_format)) {
                PrintCaptureFormat(std::cout, current_format);
            }

            std::vector<MmapBufferInfo> buffers;
            if (!device.RequestMmapBuffers(count, buffers)) {
                return EXIT_FAILURE;
            }

            PrintMmapBuffers(std::cout, buffers);
            return EXIT_SUCCESS;
        }
        if (arg1 == "--real") {
            config.capture.simulate = false;
            if (!ParseRealOptions(argc, argv, config)) {
                PrintUsage(argv[0]);
                return EXIT_FAILURE;
            }
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
    } else {
        std::ostringstream oss;
        oss << "Real capture config: device=" << config.capture.device
            << " size=" << config.capture.width << 'x' << config.capture.height
            << " format=" << config.capture.pixel_format
            << " fps=" << config.capture.fps
            << " buffers=" << config.capture.buffer_count
            << " poll_timeout_ms=" << config.capture.poll_timeout_ms;
        Log(LogLevel::kInfo, oss.str());
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
