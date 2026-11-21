#include "app/cli.hpp"

#include <cctype>
#include <iostream>
#include <string>
#include <stdexcept>

namespace app {
namespace {

bool is_space(char value) {
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

// 首字符或尾字符是空白 → 含首尾空白。
bool has_edge_whitespace(const std::string& text) {
    if (text.empty()) {
        return false;
    }
    return is_space(text.front()) || is_space(text.back());
}

}  // namespace

std::string normalize_sku(const std::string& sku) {
    // 去除首尾空白，其余字符转大写。
    std::size_t begin = 0;
    std::size_t end = sku.size();
    while (begin < end && is_space(sku[begin])) {
        ++begin;
    }
    while (end > begin && is_space(sku[end - 1])) {
        --end;
    }
    std::string normalized;
    normalized.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        normalized.push_back(static_cast<char>(
            std::toupper(static_cast<unsigned char>(sku[index]))));
    }
    return normalized;
}

bool valid_station_id(const std::string& station_id) {
    return !station_id.empty() && !has_edge_whitespace(station_id);
}

bool valid_order_id(const std::string& order_id) {
    return !order_id.empty() && !has_edge_whitespace(order_id);
}

int run() {
    std::cout << "Use the ROS 2 demo package for the end-to-end runtime.\n";
    return 0;
}

int print_trace(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--trace") {
        std::cout << "TRACE command is reserved for grading\n";
        return 0;
    }
    return run();
}

}  // namespace app
