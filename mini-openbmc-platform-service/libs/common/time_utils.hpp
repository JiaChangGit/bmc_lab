#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace common {

inline std::string iso8601Now() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&value, &utc);
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3)
           << std::setfill('0') << millis << 'Z';
    return stream.str();
}

} // namespace common
