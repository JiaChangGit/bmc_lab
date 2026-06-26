#pragma once

#include "libs/sensor/sensor_types.hpp"

#include <optional>
#include <string>

namespace sensor {

struct SensorReading {
    std::string id;
    std::string name;
    SensorType type{SensorType::temperature};
    double reading{};
    std::string unit;
    State state{State::enabled};
    Health health{Health::ok};
    std::string sourceBackend;
    std::string sourceBus;
    std::string sourceAddress;
    std::string objectPath;
    std::string lastUpdated;
    std::optional<double> upperCritical;
    std::optional<double> lowerCritical;
};

struct EventRecord {
    std::string id;
    std::string severity;
    std::string message;
    std::string timestamp;
    std::string originOfCondition;
    std::string sensorId;
    bool recovery{};
};

} // namespace sensor
