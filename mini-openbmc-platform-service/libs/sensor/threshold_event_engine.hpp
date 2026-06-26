#pragma once

#include "libs/sensor/sensor_reading.hpp"
#include "libs/sensor/threshold.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace sensor {

struct ThresholdResult {
    Health health{Health::ok};
    std::optional<EventRecord> event;
};

class ThresholdEventEngine {
  public:
    ThresholdResult evaluate(const SensorReading& reading, const Threshold& threshold);

  private:
    enum class ActiveFault { none, upper, lower };
    std::unordered_map<std::string, ActiveFault> activeFaults_;
    std::uint64_t nextEventId_{1};
};

} // namespace sensor
