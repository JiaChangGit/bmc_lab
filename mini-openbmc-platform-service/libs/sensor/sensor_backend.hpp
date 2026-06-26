#pragma once

#include "libs/common/status.hpp"
#include "libs/sensor/sensor_reading.hpp"

#include <vector>

namespace sensor {

class SensorBackend {
  public:
    virtual ~SensorBackend() = default;
    virtual common::StatusOr<std::vector<SensorReading>> readSensors() = 0;
};

} // namespace sensor
