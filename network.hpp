#pragma once

#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/url.hpp>
#include <print>
#include <string>
#include <vector>

#include "conformal_warp_image.hpp"
#include "process_complex.hpp"

namespace con {

namespace beast = boost::beast;
namespace asio = boost::asio;
namespace urls = boost::urls;

class network {
public:
    static constexpr std::uint16_t prot_num_value = 7854;

    network() = default;
    ~network() = default;

    void run() {
        asio::co_spawn(m_io_ctx, listenerv4(), asio::detached);
        asio::co_spawn(m_io_ctx, listenerv6(), asio::detached);
        m_io_ctx.run();
    }

protected:
    asio::awaitable<void> listenerv4() {
        auto executor = co_await asio::this_coro::executor;
        try {
            asio::ip::tcp::endpoint ep{asio::ip::address_v4(), prot_num_value};
            asio::ip::tcp::acceptor acceptor{executor, ep};

            for (;;) {
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
                asio::co_spawn(executor, handle_client(std::move(socket)), asio::detached);
            }
        } catch (const std::system_error& err) {
            std::println("error: {}", err.what());
        }
    }

    asio::awaitable<void> listenerv6() {
        auto executor = co_await asio::this_coro::executor;
        try {
            asio::ip::tcp::endpoint ep{asio::ip::address_v6(), prot_num_value};
            asio::ip::tcp::acceptor acceptor{executor, ep};

            for (;;) {
                asio::ip::tcp::socket socket = co_await acceptor.async_accept(asio::use_awaitable);
                asio::co_spawn(executor, handle_client(std::move(socket)), asio::detached);
            }
        } catch (const std::system_error& err) {
            std::println("error: {}", err.what());
        }
    }

    asio::awaitable<void> handle_client(asio::ip::tcp::socket socket) {
        auto executor = co_await asio::this_coro::executor;

        beast::flat_buffer buf;
        beast::http::request_parser<beast::http::string_body> req_parser;

        co_await beast::http::async_read(socket, buf, req_parser, asio::use_awaitable);

        auto req = req_parser.get();
        beast::http::response<beast::http::string_body> res;

        std::print("{} {} {}\n", req.method_string(), req.target(), req.version());
        urls::url_view url{req.target()};
        
        if (url.path() != "/handle_escher_image" && url.path() != "/handle_conformal_image") {
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
            co_return;
        }

        if (req.method() != beast::http::verb::post) {
            res.result(beast::http::status::bad_request);
            res.body() = "Please use post method";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        auto content_type = req[beast::http::field::content_type];
        if (content_type.find("image/") == std::string_view::npos) {
            res.result(beast::http::status::unsupported_media_type);
            res.body() = "Content type must be image";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        if (url.path() == "/handle_escher_image") {
            asio::co_spawn(executor, handle_escher(std::move(socket), std::move(req)), asio::detached);
        } else if (url.path() == "/handle_conformal_image") {
            asio::co_spawn(executor, handle_conformal(std::move(socket), std::move(req)), asio::detached);
        }
    }

    asio::awaitable<void> handle_escher(asio::ip::tcp::socket socket,
            beast::http::request<beast::http::string_body> req) {
        beast::http::response<beast::http::string_body> res;
        const auto& body = req.body();
        std::vector<std::uint8_t> input_data{body.begin(), body.end()};

        if (input_data.empty()) {
            res.result(beast::http::status::bad_request);
            res.body() = "Empty image data";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        std::vector<std::uint8_t> output_data{};
        bool error_occurred{false};
        try {
            output_data = handle_escher_image(std::move(input_data));
        } catch (const std::exception& e) {
            res.result(beast::http::status::internal_server_error);
            res.body() = std::string("Processing error: ") + e.what();
            res.prepare_payload();
            error_occurred = true;
        }
        if (error_occurred) {
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        res.result(beast::http::status::ok);
        res.set(beast::http::field::content_type, "image/png");
        res.body() = std::string{output_data.begin(), output_data.end()};
        res.prepare_payload();

        co_await beast::http::async_write(socket, res, asio::use_awaitable);

        beast::error_code ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
    }

    std::vector<std::uint8_t> handle_escher_image(std::vector<std::uint8_t> data) {
        cv::Mat input_img{cv::imdecode(data, cv::IMREAD_COLOR)};
        if (input_img.empty()) {
            throw std::invalid_argument("invalid image");
        }
        cv::Mat output_img{convert_escher(input_img, 0.1, 16)};
        std::vector<std::uint8_t> output_data;
        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
        if (!cv::imencode(".png", output_img, output_data, params)) {
            throw std::invalid_argument("could not covert image");
        }
        return output_data;
    }

    asio::awaitable<void> handle_conformal(asio::ip::tcp::socket socket,
            beast::http::request<beast::http::string_body> req) {
        beast::http::response<beast::http::string_body> res;
        const auto& body = req.body();
        std::vector<std::uint8_t> input_data{body.begin(), body.end()};
        urls::url_view url{req.target()};
        const auto& params = url.params();

        if (params.empty()) {
            res.result(beast::http::status::bad_request);
            res.body() = "Missing query parameters";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        if (params.find("func") == params.end()) {
            res.result(beast::http::status::bad_request);
            res.body() = "Missing 'func' query parameter";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        if (input_data.empty()) {
            res.result(beast::http::status::bad_request);
            res.body() = "Empty image data";
            res.prepare_payload();
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        std::vector<std::uint8_t> output_data{};
        bool error_occurred{false};
        try {
            auto it = params.find("func");
            std::string func_str{(*it).value};
            output_data = handle_conformal_image(std::move(input_data), std::move(func_str));
        } catch (const std::exception& e) {
            res.result(beast::http::status::internal_server_error);
            res.body() = std::string("Processing error: ") + e.what();
            res.prepare_payload();
            error_occurred = true;
        }
        if (error_occurred) {
            co_await beast::http::async_write(socket, res, asio::use_awaitable);
            co_return;
        }

        res.result(beast::http::status::ok);
        res.set(beast::http::field::content_type, "image/png");
        res.body() = std::string{output_data.begin(), output_data.end()};
        res.prepare_payload();

        co_await beast::http::async_write(socket, res, asio::use_awaitable);

        beast::error_code ec;
        socket.shutdown(asio::ip::tcp::socket::shutdown_send, ec);
    }

    std::vector<std::uint8_t> handle_conformal_image(std::vector<std::uint8_t> data,
            const std::string &func_str = "log(z)") {
        cv::Mat input_img{cv::imdecode(data, cv::IMREAD_COLOR)};
        if (input_img.empty()) {
            throw std::invalid_argument("invalid image");
        }
        cv::Mat output_img{convert_conformal(input_img, func_str)};
        std::vector<std::uint8_t> output_data;
        std::vector<int> params = {cv::IMWRITE_PNG_COMPRESSION, 3};
        if (!cv::imencode(".png", output_img, output_data, params)) {
            throw std::invalid_argument("could not covert image");
        }
        return output_data;
    }

private:
    asio::io_context m_io_ctx;
};

}
