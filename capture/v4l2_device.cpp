#include "capture/v4l2_device.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "common/logger.hpp"

namespace camera_streamer {

V4L2Device::V4L2Device(std::string device_path) : device_path_(std::move(device_path)) {}

V4L2Device::~V4L2Device() {
    Close();
}

bool V4L2Device::Open() {
    if (fd_ >= 0) {
        return true;
    }

    fd_ = open(device_path_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        std::ostringstream oss;
        oss << "Failed to open " << device_path_ << ": " << std::strerror(errno);
        Log(LogLevel::kError, oss.str());
        return false;
    }

    Log(LogLevel::kInfo, "Opened V4L2 device: " + device_path_);
    return true;
}

void V4L2Device::Close() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

bool V4L2Device::IsOpen() const {
    return fd_ >= 0;
}

bool V4L2Device::QueryCapabilities(v4l2_capability& capability) const {
    if (!IsOpen()) {
        return false;
    }
    std::memset(&capability, 0, sizeof(capability));
    return Xioctl(VIDIOC_QUERYCAP, &capability) == 0;
}

std::vector<PixelFormatInfo> V4L2Device::EnumerateCaptureFormats() const {
    std::vector<PixelFormatInfo> formats;
    if (!IsOpen()) {
        return formats;
    }

    v4l2_fmtdesc fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (fmt.index = 0; Xioctl(VIDIOC_ENUM_FMT, &fmt) == 0; ++fmt.index) {
        formats.push_back(PixelFormatInfo{
            fmt.pixelformat,
            reinterpret_cast<const char*>(fmt.description),
        });
    }
    return formats;
}

bool V4L2Device::SetCaptureFormat(std::uint32_t width, std::uint32_t height,
                                  std::uint32_t pixel_format,
                                  CaptureFormat& applied_format) const {
    if (!IsOpen()) {
        return false;
    }

    // v4l2_format 是和驱动协商视频格式的核心结构体。
    // 第一阶段我们只处理 VIDEO_CAPTURE 场景，因此 type 固定为
    // V4L2_BUF_TYPE_VIDEO_CAPTURE。
    v4l2_format format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    // 这里填入“期望值”而不是“保证值”。
    // 驱动在 S_FMT 之后可能会改写这些字段，例如：
    // - 不支持请求的分辨率时回退到最接近值
    // - 自动调整 bytesperline / sizeimage
    // - 对 field 做默认选择
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = pixel_format;
    format.fmt.pix.field = V4L2_FIELD_ANY;

    if (Xioctl(VIDIOC_S_FMT, &format) != 0) {
        std::ostringstream oss;
        oss << "VIDIOC_S_FMT failed for " << device_path_ << ": " << std::strerror(errno);
        Log(LogLevel::kError, oss.str());
        return false;
    }

    // S_FMT 返回后，format 里已经被驱动更新成“最终生效的值”。
    // 这里把关键信息抽取到轻量结构体里，后续上层就不必直接解析
    // v4l2_format 这种偏底层的 C 风格结构。
    applied_format.width = format.fmt.pix.width;
    applied_format.height = format.fmt.pix.height;
    applied_format.pixel_format = format.fmt.pix.pixelformat;
    applied_format.bytes_per_line = format.fmt.pix.bytesperline;
    applied_format.size_image = format.fmt.pix.sizeimage;
    return true;
}

bool V4L2Device::GetCaptureFormat(CaptureFormat& format) const {
    if (!IsOpen()) {
        return false;
    }

    // G_FMT 用来读取当前已经生效的采集格式。
    // 这一步很适合作为真正进入 REQBUFS/STREAMON 前的确认动作，
    // 也适合在 probe 阶段直接打印给用户看。
    v4l2_format current{};
    current.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (Xioctl(VIDIOC_G_FMT, &current) != 0) {
        std::ostringstream oss;
        oss << "VIDIOC_G_FMT failed for " << device_path_ << ": " << std::strerror(errno);
        Log(LogLevel::kError, oss.str());
        return false;
    }

    format.width = current.fmt.pix.width;
    format.height = current.fmt.pix.height;
    format.pixel_format = current.fmt.pix.pixelformat;
    format.bytes_per_line = current.fmt.pix.bytesperline;
    format.size_image = current.fmt.pix.sizeimage;
    return true;
}

void V4L2Device::DumpInfo(std::ostream& os) const {
    v4l2_capability capability{};
    if (!QueryCapabilities(capability)) {
        os << "Failed to query capabilities for " << device_path_ << '\n';
        return;
    }

    os << "Device: " << device_path_ << '\n';
    os << "Driver: " << capability.driver << '\n';
    os << "Card: " << capability.card << '\n';
    os << "Bus: " << capability.bus_info << '\n';
    os << "Version: "
       << ((capability.version >> 16) & 0xFF) << '.'
       << ((capability.version >> 8) & 0xFF) << '.'
       << (capability.version & 0xFF) << '\n';
    os << "Capabilities: 0x" << std::hex << capability.capabilities << std::dec << '\n';

    const auto formats = EnumerateCaptureFormats();
    os << "Capture formats:\n";
    for (const auto& format : formats) {
        os << "  - " << format.description
           << " (" << FourccToString(format.fourcc) << ")\n";
    }

    // probe 信息里顺手带出“当前格式”，这样不用额外执行别的命令，
    // 就能先看到驱动当前处于什么采集配置。
    CaptureFormat current_format{};
    if (GetCaptureFormat(current_format)) {
        os << "Current format:\n";
        os << "  - width: " << current_format.width << '\n';
        os << "  - height: " << current_format.height << '\n';
        os << "  - pixel_format: " << FourccToString(current_format.pixel_format) << '\n';
        os << "  - bytes_per_line: " << current_format.bytes_per_line << '\n';
        os << "  - size_image: " << current_format.size_image << '\n';
    }
}

int V4L2Device::Xioctl(unsigned long request, void* arg) const {
    int rc = 0;
    do {
        rc = ioctl(fd_, request, arg);
    } while (rc == -1 && errno == EINTR);
    return rc;
}

std::string V4L2Device::FourccToString(std::uint32_t fourcc) {
    // fourcc 本质上是 4 个字符压成一个 32-bit 整数。
    // 这里按字节拆回字符串，用于把数值格式打印成更直观的 "YUYV"、"MJPG"。
    std::string value(4, ' ');
    value[0] = static_cast<char>(fourcc & 0xFF);
    value[1] = static_cast<char>((fourcc >> 8) & 0xFF);
    value[2] = static_cast<char>((fourcc >> 16) & 0xFF);
    value[3] = static_cast<char>((fourcc >> 24) & 0xFF);
    return value;
}

}  // namespace camera_streamer
