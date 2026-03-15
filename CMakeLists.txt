cmake_minimum_required(VERSION 3.16)

project(camera_streamer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CAMERA_STREAMER_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

add_executable(camera_streamer
    app/main.cpp
    capture/v4l2_device.cpp
    pipeline/capture_worker.cpp
    pipeline/encode_worker.cpp
    pipeline/publish_worker.cpp
    pipeline/stats_worker.cpp
    encode/mjpeg_encoder_stub.cpp
    publish/rtsp_publisher_stub.cpp
)

target_include_directories(camera_streamer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(camera_streamer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

if(CAMERA_STREAMER_WARNINGS_AS_ERRORS)
    target_compile_options(camera_streamer PRIVATE -Werror)
endif()

find_package(Threads REQUIRED)
target_link_libraries(camera_streamer PRIVATE Threads::Threads)

