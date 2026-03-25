set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-linux-gnueabihf)

# Debian/Ubuntu 上通过 cross toolchain + libc6-dev-armhf-cross 安装的库，
# 往往已经由编译器内建搜索路径管理，不一定适合再额外强制指定 sysroot。
# 尤其当 libc.so 是带绝对路径的 linker script 时，显式传入
# --sysroot=/usr/arm-linux-gnueabihf 反而可能导致链接器把路径重复拼接，
# 进而出现“能看到库文件但链接阶段找不到”的问题。
#
# 因此这里把 sysroot 设计成“可选项”：
# - 默认留空，优先使用编译器自己的默认搜索路径；
# - 如果后续你有一套板子的独立 rootfs/sysroot，再通过
#   -DCMAKE_SYSROOT=/path/to/sysroot 显式传入。
set(CMAKE_SYSROOT "" CACHE PATH "Optional armhf sysroot")

set(CMAKE_C_COMPILER "/usr/bin/${TOOLCHAIN_PREFIX}-gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/${TOOLCHAIN_PREFIX}-g++")
set(CMAKE_ASM_COMPILER "/usr/bin/${TOOLCHAIN_PREFIX}-gcc")

set(CMAKE_C_COMPILER_TARGET "${TOOLCHAIN_PREFIX}")
set(CMAKE_CXX_COMPILER_TARGET "${TOOLCHAIN_PREFIX}")

set(CMAKE_FIND_ROOT_PATH
    "/usr/lib/gcc-cross/${TOOLCHAIN_PREFIX}"
    "/usr/${TOOLCHAIN_PREFIX}"
)

if(CMAKE_SYSROOT)
    list(PREPEND CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
endif()

# 查找可执行程序时，不要只在 CMAKE_FIND_ROOT_PATH 中找。
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# 只在 root path 里找库/头文件/包
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 设置环境变量 PKG_CONFIG_DIR 为空。避免宿主环境中的 
# pkg-config 默认搜索路径干扰交叉编译。
set(ENV{PKG_CONFIG_DIR} "")
if(CMAKE_SYSROOT)
    # 如果有 sysroot，配置 pkg-config 的 sysroot 和搜索路径
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
    set(ENV{PKG_CONFIG_LIBDIR}
        "${CMAKE_SYSROOT}/lib/pkgconfig:${CMAKE_SYSROOT}/share/pkgconfig:/usr/lib/${TOOLCHAIN_PREFIX}/pkgconfig"
    )
else()
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
    set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/${TOOLCHAIN_PREFIX}/pkgconfig")
endif()

# -mfloat-abi=hard 指定使用硬浮点 ABI; -mfpu=VFPv4 启用浮点计算单元
set(IMX6ULL_COMMON_FLAGS "-march=armv7-a -mfpu=VFPv4 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${IMX6ULL_COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${IMX6ULL_COMMON_FLAGS}")

# 板子上的 Debian 10 运行时未必和主机交叉工具链的 libstdc++ 版本一致。
# 为了避免出现类似
#   GLIBCXX_3.4.26 not found
# 这样的 C++ 运行时版本不匹配问题，交叉构建时默认把 libstdc++ 和 libgcc
# 静态链接进最终可执行文件。
#
# 这样做的好处是：
# 1. 省去在板子上额外部署匹配版本的 libstdc++.so；
# 2. 当前项目还很小，没有引入 ffmpeg/x264 等复杂依赖，静态链接成本低；
# 3. glibc 仍保持动态链接，兼容性风险比“全静态”小很多。
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
