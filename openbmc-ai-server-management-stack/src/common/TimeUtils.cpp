#include "common/TimeUtils.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace openbmc::common
{

std::string makeUtcTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm utcTime {};

    // 使用 UTC 而不是本機時區，避免同一筆事件在不同主機上顯示出不同時間基準。
    gmtime_r(&currentTime, &utcTime);

    std::ostringstream stream;
    stream << std::put_time(&utcTime, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

} // 命名空間 openbmc::common
