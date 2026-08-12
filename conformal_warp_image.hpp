#pragma once

#include <opencv2/opencv.hpp>
#include <iostream>
#include <complex>
#include <cmath>
#include <numbers>
#include <numeric>
#include <functional>
#include <algorithm>
#include <omp.h>

#include "process_complex.hpp"

namespace con {

using namespace std::literals;

using cplx = std::complex<double>;
constexpr cplx two_pi_i = 2i * std::numbers::pi_v<double>;

constexpr cplx calculate_C(double C) {
    return two_pi_i / (std::log(C) + two_pi_i);
}

cv::Mat convert_escher(cv::Mat img, double scalar, double C, double SSAA_scalar = 2.0) {
    cplx convert_C{calculate_C(C)};
    cv::Mat resized_img, res_img;
    cv::resize(img, resized_img, {}, SSAA_scalar, SSAA_scalar);
    int mid_x{resized_img.cols / 2}, mid_y{resized_img.rows / 2};
    cv::Mat map_x{cv::Mat::zeros(resized_img.size(), CV_32FC1)};
    cv::Mat map_y{cv::Mat::zeros(resized_img.size(), CV_32FC1)};
    #pragma omp parallel for collapse(2)
    for (int i = 0; i < resized_img.rows; ++i) {
        for (int j = 0; j < resized_img.cols; ++j) {
            cplx point = std::exp(std::log(cplx{static_cast<double>(j - mid_x) * scalar,
                static_cast<double>(mid_y - i) * scalar}) / convert_C);
            while (std::abs(point.real()) > mid_x) {
                point /= C;
            }
            while (std::abs(point.imag()) > mid_y) {
                point /= C;
            }
            std::size_t idx = 0;
            while (std::abs(point.real()) < (resized_img.cols / C) / 2.0 &&
                    std::abs(point.imag()) < (resized_img.rows / C) / 2.0 && idx++ < 5) {
                point *= C;
            }
            map_x.at<float>(i, j) = static_cast<float>(point.real() + mid_x);
            map_y.at<float>(i, j) = static_cast<float>(mid_y - point.imag());
        }
    }
    cv::remap(resized_img, res_img, map_x, map_y, cv::INTER_CUBIC, cv::BORDER_REFLECT);
    cv::resize(res_img, res_img, {}, 1 / SSAA_scalar, 1 / SSAA_scalar, cv::INTER_AREA);
    return res_img;
}

cv::Mat convert_conformal(cv::Mat img, const std::string& expr, double SSAA_scalar = 2.0) {
    cv::Mat resized_img, res_img;
    cv::resize(img, resized_img, {}, SSAA_scalar, SSAA_scalar);
    int mid_x{resized_img.cols / 2}, mid_y{resized_img.rows / 2};
    cv::Mat map_x{cv::Mat::zeros(resized_img.size(), CV_32FC1)};
    cv::Mat map_y{cv::Mat::zeros(resized_img.size(), CV_32FC1)};
    #pragma omp parallel
    {
        complex_function thread_func{expr};
        #pragma omp for collapse(2)
        for (int i = 0; i < resized_img.rows; ++i) {
            for (int j = 0; j < resized_img.cols; ++j) {
                cplx point{static_cast<double>(j - mid_x), static_cast<double>(mid_y - i)};
                cplx transformed_point;
                try {
                    transformed_point = thread_func(point);
                } catch(...) {
                    continue;
                }
                double W = static_cast<double>(resized_img.cols);
                double H = static_cast<double>(resized_img.rows);

                double x = transformed_point.real() + mid_x;
                double y = mid_y - transformed_point.imag();

                x = x - W * std::floor(x / W);
                y = y - H * std::floor(y / H);

                x = std::clamp(x, 0.0, W - 1.0);
                y = std::clamp(y, 0.0, H - 1.0);

                map_x.at<float>(i, j) = static_cast<float>(x);
                map_y.at<float>(i, j) = static_cast<float>(y);
            }
        }
    }
    cv::remap(resized_img, res_img, map_x, map_y, cv::INTER_CUBIC, cv::BORDER_REFLECT);
    cv::resize(res_img, res_img, {}, 1 / SSAA_scalar, 1 / SSAA_scalar, cv::INTER_AREA);
    return res_img;
}

}
