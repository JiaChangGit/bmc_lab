#pragma once

#include "libs/sensor/sensor_types.hpp"

#include <optional>

namespace sensor {

struct Threshold {
    std::optional<double> upperCritical;
    std::optional<double> lowerCritical;
    double hysteresis{};
    Health assertedHealth{Health::critical};
};

} // namespace sensor
