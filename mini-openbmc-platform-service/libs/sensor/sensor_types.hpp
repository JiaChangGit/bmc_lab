#pragma once

#include <string>

namespace sensor {

enum class Health { ok, warning, critical, unknown };
enum class State { enabled, unavailable, disabled };
enum class SensorType { temperature, power, fanTach, voltage, network, count, link };

inline std::string toString(Health value) {
    switch (value) {
    case Health::ok:
        return "OK";
    case Health::warning:
        return "Warning";
    case Health::critical:
        return "Critical";
    case Health::unknown:
        return "Unknown";
    }
    return "Unknown";
}

inline std::string toString(State value) {
    switch (value) {
    case State::enabled:
        return "Enabled";
    case State::unavailable:
        return "Unavailable";
    case State::disabled:
        return "Disabled";
    }
    return "Unavailable";
}

inline std::string toString(SensorType value) {
    switch (value) {
    case SensorType::temperature:
        return "Temperature";
    case SensorType::power:
        return "Power";
    case SensorType::fanTach:
        return "FanTach";
    case SensorType::voltage:
        return "Voltage";
    case SensorType::network:
        return "Network";
    case SensorType::count:
        return "Count";
    case SensorType::link:
        return "Link";
    }
    return "Unknown";
}

} // namespace sensor
