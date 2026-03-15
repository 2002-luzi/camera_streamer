#pragma once

#include <linux/videodev2.h>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

namespace camera_streamer {

struct PixelFormatInfo {
    std::uint32_t fourcc{0};
    std::string description;
};

// 统一承载当前视频采集格式的关键信息。
// 这里刻意只保留第一阶段最关心的字段，方便：
// 1. 在 CLI 中打印驱动最终接受的格式；
// 2. 后续把格式信息继续传给真实采集逻辑；
// 3. 避免在业务层直接暴露一整块 v4l2_format 结构体。
struct CaptureFormat {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint32_t pixel_format{0};
    std::uint32_t bytes_per_line{0};
    std::uint32_t size_image{0};
};

class V4L2Device {
public:
    explicit V4L2Device(std::string device_path);
    ~V4L2Device();

    V4L2Device(const V4L2Device&) = delete;
    V4L2Device& operator=(const V4L2Device&) = delete;

    bool Open();
    void Close();
    bool IsOpen() const;

    bool QueryCapabilities(v4l2_capability& capability) const;
    std::vector<PixelFormatInfo> EnumerateCaptureFormats() const;
    // 向驱动申请一个采集格式。
    // 注意：V4L2 的 S_FMT 不保证“严格按请求值生效”，驱动可能会调整宽高、
    // stride 或 sizeimage，所以这里把“驱动最终接受的结果”通过
    // applied_format 返回给调用方。
    bool SetCaptureFormat(std::uint32_t width, std::uint32_t height, std::uint32_t pixel_format,
                          CaptureFormat& applied_format) const;
    // 读取设备当前生效的采集格式。
    // 这个接口用于：
    // 1. probe 时直接查看当前状态；
    // 2. 后续在真正开始采集前再次确认格式；
    // 3. 和 SetCaptureFormat 的结果做一致性对照。
    bool GetCaptureFormat(CaptureFormat& format) const;
    void DumpInfo(std::ostream& os) const;

    const std::string& device_path() const { return device_path_; }
    // 把 fourcc 整数转成类似 "YUYV" 的可读字符串，便于日志和 CLI 展示。
    static std::string FourccToString(std::uint32_t fourcc);

private:
    int Xioctl(unsigned long request, void* arg) const;

    std::string device_path_;
    int fd_{-1};
};

}  // namespace camera_streamer
