#pragma once

#include "libs/common/status.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace hwmon {

struct HwmonReadings {
    std::int64_t temperatureMillic{};
    std::int64_t voltageMillivolt{};
    std::int64_t fanRpm{};
    std::string temperatureLabel;
    std::string voltageLabel;
    std::string fanLabel;
};

class HwmonSensorBackend {
  public:
    explicit HwmonSensorBackend(std::filesystem::path root);
    static common::StatusOr<std::filesystem::path>
        discover(const std::filesystem::path& root = "/sys/class/hwmon");
    common::StatusOr<HwmonReadings> read() const;
    common::Status setFault(const std::string& fault) const;

  private:
    std::filesystem::path root_;
};

} // namespace hwmon
