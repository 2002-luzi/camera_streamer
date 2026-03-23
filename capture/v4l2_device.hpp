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

// 承载驱动当前接受的 timeperframe。
// V4L2 常把帧率表示成一个分数：
//   fps = denominator / numerator
// 常见情况是 numerator=1, denominator=30，表示 30 fps。
struct CaptureFrameRate {
    std::uint32_t numerator{0};
    std::uint32_t denominator{0};
};

// 描述一个已经由驱动创建好的 V4L2 mmap buffer。
// 这一步还不真正调用 mmap()，只先把 QUERYBUF 返回的关键信息抽出来。
// 后续真正进入采集阶段时，会用这里的 offset/length 去调用 mmap。
struct MmapBufferInfo {
    std::uint32_t index{0};
    std::uint32_t length{0};
    std::uint32_t bytes_used{0};
    std::uint32_t offset{0};
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
    // 通过 VIDIOC_S_PARM 请求 timeperframe。
    // 对于大多数 capture 设备，用户更习惯输入 fps，但驱动真正使用的是
    // 一个分数 timeperframe，因此这里把驱动最终接受的分子/分母回读出来。
    bool SetFrameRate(std::uint32_t fps, CaptureFrameRate& applied_rate) const;
    // 让驱动为 VIDEO_CAPTURE + MMAP 模式分配一组 buffer，并把每个 buffer 的
    // QUERYBUF 结果导出出来。
    // 这一步是“真实 V4L2 采集”前非常关键的准备动作：
    // 1. REQBUFS 让驱动建立 vb2 / DMA buffer 池；
    // 2. QUERYBUF 告诉用户态每个 buffer 的大小和 mmap offset；
    // 3. 后续用户态才能用 mmap() 把这些 buffer 映射进自己的地址空间。
    bool RequestMmapBuffers(std::uint32_t requested_count,
                            std::vector<MmapBufferInfo>& buffers) const;
    // 用 REQBUFS(count=0) 释放当前设备上已经分配的 MMAP buffer。
    // 这一步通常放在 munmap() 之后，让驱动端的 buffer 池也一并回收。
    bool ReleaseMmapBuffers() const;
    void DumpInfo(std::ostream& os) const;

    const std::string& device_path() const { return device_path_; }
    // 暴露底层 fd，供 poll()/mmap() 这类更贴近 Linux 系统调用的操作使用。
    // 这里不把 poll/mmap 再包一层，是为了让 CaptureWorker 能更直接地表达
    // “事件等待 + 内存映射 + QBUF/DQBUF”的采集状态机。
    int fd() const { return fd_; }
    // 把 fourcc 整数转成类似 "YUYV" 的可读字符串，便于日志和 CLI 展示。
    static std::string FourccToString(std::uint32_t fourcc);

private:
    int Xioctl(unsigned long request, void* arg) const;

    std::string device_path_;
    int fd_{-1};
};

}  // namespace camera_streamer
