#pragma once

#include "libs/sensor/sensor_reading.hpp"

#include <nlohmann/json.hpp>

namespace redfish {

nlohmann::json sensorToDbusProperties(const sensor::SensorReading& reading);
nlohmann::json eventToDbusProperties(const sensor::EventRecord& event);
nlohmann::json sensorPropertiesToRedfish(const nlohmann::json& properties);
nlohmann::json eventPropertiesToRedfish(const nlohmann::json& properties);

} // namespace redfish
