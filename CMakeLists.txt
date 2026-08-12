cmake_minimum_required(VERSION 3.30)

set(CMAKE_CXX_STANDARD 23)

project(conformal_canvas)

cmake_policy(SET CMP0167 NEW)

# vcpkg 动态 triplet 下静态开关会导致 find_package 失败, 仅非 Windows 启用
if(NOT WIN32)
    set(Boost_USE_STATIC_LIBS ON)
endif()

find_package(OpenMP REQUIRED)
find_package(OpenCV REQUIRED)
find_package(Boost 1.70 REQUIRED COMPONENTS url)

# 优先使用系统安装的 muparserx, 缺失时 (如 Windows/vcpkg) 从源码构建
find_package(muparserx QUIET)
if(muparserx_FOUND)
    set(MUPARSERX_LIBS ${muparserx_LIBRARIES})
    set(MUPARSERX_INCLUDES ${muparserx_INCLUDE_DIRS})
else()
    message(STATUS "muparserx not found, fetching from source")
    include(FetchContent)
    # muparserx 要求 cmake>=2.8.12, CMake 4.x 下需放宽最低策略版本
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(muparserx
        URL https://github.com/beltoforion/muparserx/archive/refs/tags/v4.0.12.tar.gz)
    FetchContent_MakeAvailable(muparserx)
    set(MUPARSERX_LIBS muparserx)
    # 源码目录为 parser/*.h, 而代码包含 <muparserx/mpParser.h>, 复制出带 muparserx/ 前缀的 include 目录
    set(MUPARSERX_INCLUDES ${CMAKE_BINARY_DIR}/muparserx-include)
    file(COPY ${muparserx_SOURCE_DIR}/parser/
        DESTINATION ${MUPARSERX_INCLUDES}/muparserx
        FILES_MATCHING PATTERN "*.h")
endif()

add_executable(conformal_canvas main.cpp
    conformal_warp_image.hpp
    gif_utils.hpp
    network.hpp
    process_complex.hpp)
target_include_directories(conformal_canvas PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    ${MUPARSERX_INCLUDES})
target_link_libraries(conformal_canvas PRIVATE
    ${OpenCV_LIBS}
    OpenMP::OpenMP_CXX
    Boost::boost
    Boost::url
    ${MUPARSERX_LIBS})
