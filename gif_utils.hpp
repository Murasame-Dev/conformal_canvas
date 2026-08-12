#pragma once

#include <opencv2/opencv.hpp>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "third_party/gif.h"

namespace con {

// GIF 帧延迟, 单位为 1/100 秒 (100ms)
inline constexpr std::uint32_t gif_frame_delay_cs = 10;

// 生成唯一的临时文件路径 (并发请求互不冲突)
inline std::filesystem::path make_temp_path(const std::string& ext) {
    static std::atomic<std::uint64_t> counter{0};
    std::random_device rd;
    return std::filesystem::temp_directory_path()
        / std::format("conformal_canvas_{}_{}{}", rd(), counter.fetch_add(1), ext);
}

// 解码输入图片为帧序列: GIF 输入解码全部帧, 其余格式返回单帧
// 注: OpenCV 的 imreadmulti 仅支持文件路径, GIF 需经临时文件中转
inline std::vector<cv::Mat> decode_frames(const std::vector<std::uint8_t>& data,
        std::string_view content_type) {
    if (content_type.find("image/gif") != std::string_view::npos) {
        const auto tmp_path = make_temp_path(".gif");
        {
            std::ofstream ofs{tmp_path, std::ios::binary};
            ofs.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
        }
        std::vector<cv::Mat> frames;
        const bool ok = cv::imreadmulti(tmp_path.string(), frames, cv::IMREAD_COLOR);
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        if (!ok || frames.empty()) {
            throw std::invalid_argument("could not decode gif image");
        }
        return frames;
    }
    cv::Mat img{cv::imdecode(data, cv::IMREAD_COLOR)};
    if (img.empty()) {
        throw std::invalid_argument("invalid image");
    }
    return {img};
}

// 将帧序列编码为 GIF (gif.h 仅支持写入文件, 故经临时文件中转)
inline std::vector<std::uint8_t> encode_gif_frames(const std::vector<cv::Mat>& frames,
        std::uint32_t delay_cs = gif_frame_delay_cs) {
    if (frames.empty()) {
        throw std::invalid_argument("no frames to encode");
    }
    const auto width = static_cast<std::uint32_t>(frames.front().cols);
    const auto height = static_cast<std::uint32_t>(frames.front().rows);

    const auto tmp_path = make_temp_path(".gif");

    {
        GifWriter writer{};
        if (!GifBegin(&writer, tmp_path.string().c_str(), width, height, delay_cs)) {
            throw std::runtime_error("could not open temporary gif file");
        }
        cv::Mat rgba;
        for (const auto& frame : frames) {
            if (frame.empty()) {
                continue;
            }
            cv::Mat src{frame};
            if (static_cast<std::uint32_t>(frame.cols) != width
                    || static_cast<std::uint32_t>(frame.rows) != height) {
                cv::resize(frame, src, {static_cast<int>(width), static_cast<int>(height)});
            }
            cv::cvtColor(src, rgba, cv::COLOR_BGR2RGBA);
            if (!GifWriteFrame(&writer, rgba.data, width, height, delay_cs)) {
                GifEnd(&writer);
                throw std::runtime_error("could not write gif frame");
            }
        }
        if (!GifEnd(&writer)) {
            throw std::runtime_error("could not finalize gif file");
        }
    }

    std::ifstream ifs{tmp_path, std::ios::binary};
    std::vector<std::uint8_t> output{std::istreambuf_iterator<char>{ifs},
        std::istreambuf_iterator<char>{}};
    std::error_code ec;
    std::filesystem::remove(tmp_path, ec);
    if (output.empty()) {
        throw std::runtime_error("could not read encoded gif");
    }
    return output;
}

}
