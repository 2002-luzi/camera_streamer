# camera_streamer

`camera_streamer` is a starter C++17 project for an `i.MX6ULL + OV5640` video
pipeline. The current focus is:

- native `V4L2` learning and device probing
- C++ multi-threaded pipeline structure
- clear separation between capture, encode, and publish stages
- an easy upgrade path toward `RTSP -> MediaMTX -> RTSP/WebRTC`

This initial version intentionally keeps the publish and encode backends as
stubs so the project can compile without extra multimedia development packages.

## Current Status

Implemented:

- `CMake` based standalone project
- `V4L2Device` probe helper
- bounded queue for producer/consumer experiments
- capture / encode / publish / stats worker skeletons
- simulation mode so the pipeline can be exercised on a host machine

Planned next:

1. implement real `VIDIOC_REQBUFS/QUERYBUF/QBUF/DQBUF/STREAMON`
2. add software `MJPEG` encoding
3. publish to `MediaMTX` through `RTSP`

## Build

```bash
cd /home/luzi/linux_dirver/camera_streamer
cmake -S . -B build
cmake --build build -j
```

## Cross Build For i.MX6ULL

The project now includes an ARM Linux toolchain file for `i.MX6ULL` style
`armhf` targets:

```bash
cd /home/luzi/linux_dirver/camera_streamer
cmake -S . -B build-imx6ull \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/imx6ull-armhf.cmake
cmake --build build-imx6ull -j
```

If the host is still missing the cross C++ compiler, install these packages:

```bash
sudo apt-get update
sudo apt-get install -y \
  g++-arm-linux-gnueabihf \
  gcc-arm-linux-gnueabihf \
  libc6-dev-armhf-cross \
  libstdc++-9-dev-armhf-cross \
  pkg-config-arm-linux-gnueabihf
```

There is also a helper script:

```bash
./scripts/build-imx6ull.sh
```

The default sysroot used by the toolchain file is:

```text
/usr/arm-linux-gnueabihf
```

## Run

Probe a camera node:

```bash
./build/camera_streamer --probe /dev/video0
```

Apply a capture format and print what the driver accepted:

```bash
./build/camera_streamer --set-format /dev/video0 640 480 YUYV
```

Request V4L2 mmap buffers and print the `REQBUFS + QUERYBUF` result:

```bash
./build/camera_streamer --request-buffers /dev/video0 4
```

Run the threaded skeleton in simulation mode:

```bash
./build/camera_streamer
```

Stop it with `Ctrl+C`.

## Suggested Board-Side Runtime Workflow

1. use `--probe /dev/video0` to confirm format support
2. use `--set-format /dev/video0 640 480 YUYV` to confirm `S_FMT/G_FMT`
3. use `--request-buffers /dev/video0 4` to confirm `REQBUFS + QUERYBUF`
4. implement real capture in `CaptureWorker`
5. keep queue sizes small and observe backpressure behavior
6. add `MJPEG` encoding first
7. push `RTSP` to a PC-side `MediaMTX`

## Cross Toolchain Notes

The intended Linux cross toolchain prefix is:

```text
arm-linux-gnueabihf-
```

This is the right family for `i.MX6ULL` Debian user-space binaries.
Do not use `arm-none-eabi-*` for this project; that toolchain is for bare-metal
targets and not for Linux user-space applications.
