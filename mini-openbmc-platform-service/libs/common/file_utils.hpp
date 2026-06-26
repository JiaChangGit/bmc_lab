#pragma once

#include "libs/common/status.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace common {

inline StatusOr<std::string> readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        return Status::error(StatusCode::notFound, "cannot open " + path.string());
    }
    std::ostringstream data;
    data << input.rdbuf();
    std::string value = data.str();
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

inline Status writeTextFile(const std::filesystem::path& path, const std::string& value) {
    std::ofstream output(path);
    if (!output) {
        return Status::error(StatusCode::ioError, "cannot write " + path.string());
    }
    output << value;
    if (!output) {
        return Status::error(StatusCode::ioError, "write failed for " + path.string());
    }
    return Status::okStatus();
}

} // namespace common
