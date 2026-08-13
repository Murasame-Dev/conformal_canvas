#pragma once

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/url.hpp>
#include <chrono>
#include <cstdio>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "conformal_warp_image.hpp"
#include "gif_utils.hpp"
#include "process_complex.hpp"

namespace con {

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace urls = boost::urls;

// beast/urls 的 string_view 类型不保证可被 std::format 直接格式化, 统一转换
template <typename S>
std::string_view sv(const S& s) {
    return {s.data(), s.size()};
}

// 带时间戳的控制台日志: [YYYY-MM-DD HH:MM:SS] [component] message
template <typename... Args>
void log(std::string_view component, std::format_string<Args...> fmt, Args&&... args) {
    const auto now = std::chrono::system_clock::now();
    const auto days = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day ymd{days};
    const std::chrono::hh_mm_ss tod{std::chrono::floor<std::chrono::seconds>(now) - days};
    std::println("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}] [{}] {}",
        static_cast<int>(ymd.year()), static_cast<unsigned>(ymd.month()),
        static_cast<unsigned>(ymd.day()),
        tod.hours().count(), tod.minutes().count(), tod.seconds().count(),
        component, std::format(fmt, std::forward<Args>(args)...));
    // 管道/重定向下 stdout 为全缓冲, 显式刷新保证日志实时可见 (如 docker logs)
    std::fflush(stdout);
}

// 输出格式 (format 查询参数)
enum class output_format : std::uint8_t { png, jpeg, webp, bmp, gif };

inline constexpr std::string_view output_format_extension(output_format f) {
    switch (f) {
    case output_format::png: return ".png";
    case output_format::jpeg: return ".jpg";
    case output_format::webp: return ".webp";
    case output_format::bmp: return ".bmp";
    case output_format::gif: return ".gif";
    }
    return ".png";
}

inline constexpr std::string_view output_format_mime(output_format f) {
    switch (f) {
    case output_format::png: return "image/png";
    case output_format::jpeg: return "image/jpeg";
    case output_format::webp: return "image/webp";
    case output_format::bmp: return "image/bmp";
    case output_format::gif: return "image/gif";
    }
    return "image/png";
}

inline std::optional<output_format> parse_output_format(std::string_view s) {
    if (s == "png") return output_format::png;
    if (s == "jpeg" || s == "jpg") return output_format::jpeg;
    if (s == "webp") return output_format::webp;
    if (s == "bmp") return output_format::bmp;
    if (s == "gif") return output_format::gif;
    return std::nullopt;
}

// 请求 Content-Type 枚举
enum class media_type : std::uint8_t {
    image_gif, image_png, image_jpeg, image_webp, image_bmp, image_other, non_image
};

inline media_type parse_media_type(std::string_view s) {
    if (const auto pos = s.find(';'); pos != std::string_view::npos) {
        s = s.substr(0, pos);
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    if (!s.starts_with("image/")) {
        return media_type::non_image;
    }
    const auto sub = s.substr(std::string_view{"image/"}.size());
    if (sub == "gif") return media_type::image_gif;
    if (sub == "png") return media_type::image_png;
    if (sub == "jpeg" || sub == "jpg") return media_type::image_jpeg;
    if (sub == "webp") return media_type::image_webp;
    if (sub == "bmp") return media_type::image_bmp;
    return media_type::image_other;
}

inline constexpr std::string_view usage_text = R"(Conformal Canvas - 复变函数图像变换服务

端点:
  GET  /                               显示本帮助
  POST /handle_escher_image            Escher 风格变换
  POST /handle_conformal_image?func=   自定义复变函数变换 (变量名为 z, 默认 log(z))

通用查询参数:
  format   输出格式: png (默认) | jpeg | webp | bmp | gif (动画逐帧变换, 帧延迟 100ms)
           输入为 GIF 且输出非 gif 时仅处理第一帧

请求: Content-Type 为 image/*, 请求体为原始图片二进制
响应: 200 + image/png 或 image/gif
      400 方法/参数错误 | 404 路径不存在 | 415 Content-Type 非 image/* | 500 处理失败

示例:
  curl -X POST -H "Content-Type: image/png" --data-binary @input.png \
      http://127.0.0.1:7854/handle_escher_image --output out.png

  curl -X POST -H "Content-Type: image/png" --data-binary @input.png \
      "http://127.0.0.1:7854/handle_conformal_image?func=exp%28z%29" --output out.png

  curl -X POST -H "Content-Type: image/gif" --data-binary @input.gif \
      "http://127.0.0.1:7854/handle_escher_image?format=gif" --output out.gif
)";

class network {
public:
    static constexpr std::uint16_t prot_num_value = 7854;

    network() = default;
    ~network() = default;

    void run() {
        asio::co_spawn(m_io_ctx, listenerv4(), asio::detached);
        asio::co_spawn(m_io_ctx, listenerv6(), asio::detached);
        log("network", "starting, port {}", prot_num_value);
        m_io_ctx.run();
    }

protected:
    asio::awaitable<void> listenerv4() {
        auto executor = co_await asio::this_coro::executor;
        try {
            asio::ip::tcp::endpoint ep{asio::ip::address_v4(), prot_num_value};
            asio::ip::tcp::acceptor acceptor{executor, ep};
            log("listener v4", "listening on 0.0.0.0:{}", prot_num_value);

            for (;;) {
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
                try {
                    auto ep = socket.remote_endpoint();
                    log("listener v4", "accepted {}:{}", ep.address().to_string(), ep.port());
                } catch (const std::exception &e) {
                    log("listener v4", "accepted connection (remote endpoint unavailable): {}",
                        e.what());
                }
                asio::co_spawn(executor, handle_client(std::move(socket)), asio::detached);
            }
        } catch (const std::system_error& err) {
            log("listener v4", "error: {}", err.what());
        }
    }

    asio::awaitable<void> listenerv6() {
        auto executor = co_await asio::this_coro::executor;
        try {
            asio::ip::tcp::endpoint ep{asio::ip::address_v6(), prot_num_value};
            asio::ip::tcp::acceptor acceptor{executor, ep};
            log("listener v6", "listening on [::]:{}", prot_num_value);

            for (;;) {
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
                try {
                    auto ep = socket.remote_endpoint();
                    log("listener v6", "accepted {}:{}", ep.address().to_string(), ep.port());
                } catch (const std::exception &e) {
                    log("listener v6", "accepted connection (remote endpoint unavailable): {}",
                        e.what());
                }
                asio::co_spawn(executor, handle_client(std::move(socket)), asio::detached);
            }
        } catch (const std::system_error& err) {
            log("listener v6", "error: {}", err.what());
        }
    }

    asio::awaitable<void> handle_client(asio::ip::tcp::socket socket) {
        auto executor = co_await asio::this_coro::executor;

        beast::flat_buffer buf;
        beast::http::request_parser<beast::http::string_body> req_parser;
        req_parser.body_limit(1024 * 1024 * 50);

        beast::http::response<beast::http::string_body> res;
        {
            bool read_successful{false};
            try {
                co_await beast::http::async_read(socket, buf, req_parser, asio::use_awaitable);
                read_successful = true;
            } catch (const std::exception& e) {
                log("handle_client", "error reading request body: {}", e.what());
                res.result(beast::http::status::bad_request);
                res.body() = std::string("Error reading request body: ") + e.what();
                res.prepare_payload();
            }
            if (!read_successful) {
                co_await beast::http::async_write(socket, res, asio::use_awaitable);
                co_return;
            }
        }

        auto req = req_parser.get();

        log("handle_client", "method: {}, target: {}, version: {}.{}, body: {} bytes",
            sv(req.method_string()), sv(req.target()), req.version() / 10, req.version() % 10,
            req.body().size());
        urls::url_view url{req.target()};

        if (url.path() == "/") {
            if (req.method() != beast::http::verb::get) {
                log("handle_client", "rejected: root path only supports GET");
                res.result(beast::http::status::bad_request);
                res.body() = "Please use get method";
                res.prepare_payload();
                co_await beast::http::async_write(socket, res, asio::use_awaitable);
                co_return;
            }
            res.result(beast::http::status::ok);
            res.set(beast::http::field::content_type, "text/plain; charset=utf-8");
            res.body() = usage_text;
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            log("handle_client", "usage sent, {} bytes", res.body().size());
            co_return;
        }

        if (url.path() != "/handle_escher_image" && url.path() != "/handle_conformal_image") {
            log("handle_client", "rejected: unknown path {}", sv(url.path()));
            res.result(beast::http::status::not_found);
            res.body() = "404 Not found";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        if (req.method() == beast::http::verb::options) {
            res.result(beast::http::status::ok);

            auto origin = req[beast::http::field::origin];
            if (!origin.empty()) {
                res.set(beast::http::field::access_control_allow_origin, origin);
            } else {
                res.set(beast::http::field::access_control_allow_origin, "*");
            }
            res.set(beast::http::field::access_control_allow_methods, "POST, OPTIONS");
            res.set(beast::http::field::access_control_allow_headers, "Content-Type, Origin");
            res.set(beast::http::field::access_control_allow_credentials, "true");
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            log("handle_client", "cors preflight answered for {}", sv(url.path()));
            co_return;
        }

        if (req.method() != beast::http::verb::post) {
            log("handle_client", "rejected: method {} is not POST", sv(req.method_string()));
            res.result(beast::http::status::bad_request);
            res.body() = "Please use post method";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        auto content_type = req[beast::http::field::content_type];
        const auto mtype = parse_media_type(sv(content_type));
        if (mtype == media_type::non_image) {
            log("handle_client", "rejected: content type {} is not image/*", sv(content_type));
            res.result(beast::http::status::unsupported_media_type);
            res.body() = "Content type must be image";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        output_format format{output_format::png};
        {
            const auto& params = url.params();
            auto it = params.find("format");
            if (it != params.end()) {
                const auto parsed = parse_output_format((*it).value);
                if (!parsed) {
                    log("handle_client", "rejected: unsupported format {}", sv((*it).value));
                    res.result(beast::http::status::bad_request);
                    res.body() = "Unsupported 'format' query parameter, "
                        "use png, jpeg, webp, bmp or gif";
                    res.prepare_payload();
                    co_await beast::http::async_write(socket, res, asio::use_awaitable);
                    co_return;
                }
                format = *parsed;
            }
        }

        if (url.path() == "/handle_escher_image") {
            log("handle_client", "spawning escher handler, format {}",
                output_format_mime(format));
            asio::co_spawn(executor, handle_escher(std::move(socket),
                std::move(req), mtype, format), asio::detached);
        } else if (url.path() == "/handle_conformal_image") {
            log("handle_client", "spawning conformal handler, format {}",
                output_format_mime(format));
            asio::co_spawn(executor, handle_conformal(std::move(socket),
                std::move(req), mtype, format), asio::detached);
        }
    }

    asio::awaitable<void> handle_escher(asio::ip::tcp::socket socket,
            beast::http::request<beast::http::string_body> req,
            media_type mtype, output_format format) {
        beast::http::response<beast::http::string_body> res;
        const auto& body = req.body();
        std::vector<std::uint8_t> input_data{body.begin(), body.end()};

        if (input_data.empty()) {
            log("handle_escher", "rejected: empty image data");
            res.result(beast::http::status::bad_request);
            res.body() = "Empty image data";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        std::vector<std::uint8_t> output_data{};
        {
            bool error_occurred{false};
            try {
                log("handle_escher", "received {} bytes, media_type {}", input_data.size(),
                    static_cast<int>(mtype));
                const auto t0 = std::chrono::steady_clock::now();
                output_data = handle_escher_image(std::move(input_data), format);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                log("handle_escher", "processed in {} ms, output {} bytes", ms, output_data.size());
            } catch (const std::exception& e) {
                log("handle_escher", "processing error: {}", e.what());
                res.result(beast::http::status::internal_server_error);
                res.body() = std::string("Processing error: ") + e.what();
                res.prepare_payload();
                error_occurred = true;
            }
            if (error_occurred) {
                co_await beast::http::async_write(socket, res, asio::use_awaitable);
                co_return;
            }
        }

        res.result(beast::http::status::ok);
        res.set(beast::http::field::content_type, output_format_mime(format));
        res.body() = std::string{output_data.begin(), output_data.end()};
        res.prepare_payload();

        co_await beast::http::async_write(socket, res, asio::use_awaitable);
        log("handle_escher", "response sent, {} bytes", res.body().size());

        beast::error_code ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
        if (ec) {
            log("handle_escher", "socket shutdown error: {}", ec.message());
        } else {
            log("handle_escher", "socket shutdown clean");
        }
    }

    std::vector<std::uint8_t> handle_escher_image(std::vector<std::uint8_t> data,
            output_format format) {
        auto frames = decode_frames(data);
        if (format == output_format::gif) {
            log("handle_escher", "converting {} frame(s) to gif", frames.size());
            for (auto& frame : frames) {
                frame = convert_escher(frame, 0.1, 16);
            }
            return encode_gif_frames(frames);
        }
        cv::Mat output_img{convert_escher(frames.front(), 0.1, 16)};
        return encode_image(output_img, output_format_extension(format));
    }

    asio::awaitable<void> handle_conformal(asio::ip::tcp::socket socket,
            beast::http::request<beast::http::string_body> req,
            media_type mtype, output_format format) {
        beast::http::response<beast::http::string_body> res;
        const auto& body = req.body();
        std::vector<std::uint8_t> input_data{body.begin(), body.end()};
        urls::url_view url{req.target()};
        const auto& params = url.params();

        if (input_data.empty()) {
            log("handle_conformal", "rejected: empty image data");
            res.result(beast::http::status::bad_request);
            res.body() = "Empty image data";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        std::vector<std::uint8_t> output_data{};
        {
            bool error_occurred{false};
            try {
                std::string func_str{"log(z)"};
                auto it = params.find("func");
                if (it != params.end()) {
                    func_str = (*it).value;
                }
                log("handle_conformal", "received {} bytes, func: {}, media_type {}",
                    input_data.size(), func_str, static_cast<int>(mtype));
                const auto t0 = std::chrono::steady_clock::now();
                output_data = handle_conformal_image(std::move(input_data), func_str, format);
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                log("handle_conformal", "processed in {} ms, output {} bytes", ms, output_data.size());
            } catch (const std::exception& e) {
                log("handle_conformal", "processing error: {}", e.what());
                res.result(beast::http::status::internal_server_error);
                res.body() = std::string("Processing error: ") + e.what();
                res.prepare_payload();
                error_occurred = true;
            }
            if (error_occurred) {
                co_await beast::http::async_write(socket, res, asio::use_awaitable);
                co_return;
            }
        }

        res.result(beast::http::status::ok);
        res.set(beast::http::field::content_type, output_format_mime(format));
        res.body() = std::string{output_data.begin(), output_data.end()};
        res.prepare_payload();

        co_await beast::http::async_write(socket, res, asio::use_awaitable);
        log("handle_conformal", "response sent, {} bytes", res.body().size());

        beast::error_code ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
        if (ec) {
            log("handle_conformal", "socket shutdown error: {}", ec.message());
        } else {
            log("handle_conformal", "socket shutdown clean");
        }
    }

    std::vector<std::uint8_t> handle_conformal_image(std::vector<std::uint8_t> data,
            const std::string& func_str = "log(z)",
            output_format format = output_format::png) {
        auto frames = decode_frames(data);
        if (format == output_format::gif) {
            log("handle_conformal", "converting {} frame(s) to gif", frames.size());
            for (auto& frame : frames) {
                frame = convert_conformal(frame, func_str);
            }
            return encode_gif_frames(frames);
        }
        cv::Mat output_img{convert_conformal(frames.front(), func_str)};
        return encode_image(output_img, output_format_extension(format));
    }

private:
    asio::io_context m_io_ctx;
};

}
