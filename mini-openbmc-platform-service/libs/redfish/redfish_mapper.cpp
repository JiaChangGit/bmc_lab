#include "libs/redfish/redfish_mapper.hpp"

namespace redfish {

nlohmann::json sensorToDbusProperties(const sensor::SensorReading& reading) {
    return {
        {"Kind", "Sensor"},
        {"Id", reading.id},
        {"Name", reading.name},
        {"Type", sensor::toString(reading.type)},
        {"Reading", reading.reading},
        {"Unit", reading.unit},
        {"State", sensor::toString(reading.state)},
        {"Health", sensor::toString(reading.health)},
        {"HealthRollup", sensor::toString(reading.health)},
        {"LastError", ""},
        {"SourceBackend", reading.sourceBackend},
        {"SourceBus", reading.sourceBus},
        {"SourceAddress", reading.sourceAddress},
        {"ObjectPath", reading.objectPath},
        {"LastUpdated", reading.lastUpdated},
        {"UpperCritical", reading.upperCritical ? nlohmann::json(*reading.upperCritical)
                                                 : nlohmann::json(nullptr)},
        {"LowerCritical", reading.lowerCritical ? nlohmann::json(*reading.lowerCritical)
                                                 : nlohmann::json(nullptr)},
    };
}

nlohmann::json eventToDbusProperties(const sensor::EventRecord& event) {
    return {
        {"Kind", "Event"},
        {"Id", event.id},
        {"Severity", event.severity},
        {"Message", event.message},
        {"Timestamp", event.timestamp},
        {"OriginOfCondition", event.originOfCondition},
        {"SensorId", event.sensorId},
        {"Recovery", event.recovery},
    };
}

nlohmann::json sensorPropertiesToRedfish(const nlohmann::json& properties) {
    return {
        {"@odata.type", "#Sensor.v1_0_0.Sensor"},
        {"@odata.id", "/redfish/v1/Chassis/GPU0/Sensors/" +
                          properties.value("Id", "")},
        {"Id", properties.value("Id", "")},
        {"Name", properties.value("Name", "")},
        {"Reading", properties.value("Reading", 0.0)},
        {"ReadingUnits", properties.value("Unit", "")},
        {"Status",
         {{"State", properties.value("State", "Unavailable")},
          {"Health", properties.value("Health", "Unknown")}}},
        {"UpperThresholdCritical",
         properties.contains("UpperCritical") ? properties["UpperCritical"]
                                               : nlohmann::json(nullptr)},
        {"LowerThresholdCritical",
         properties.contains("LowerCritical") ? properties["LowerCritical"]
                                               : nlohmann::json(nullptr)},
        {"Oem",
         {{"MiniOpenBMC",
           {{"SourceBackend", properties.value("SourceBackend", "")},
            {"SourceBus", properties.value("SourceBus", "")},
            {"SourceAddress", properties.value("SourceAddress", "")},
            {"ObjectPath", properties.value("ObjectPath", "")}}}}},
    };
}

nlohmann::json eventPropertiesToRedfish(const nlohmann::json& properties) {
    return {
        {"@odata.type", "#LogEntry.v1_9_0.LogEntry"},
        {"Id", properties.value("Id", "")},
        {"Severity", properties.value("Severity", "Warning")},
        {"Message", properties.value("Message", "")},
        {"Created", properties.value("Timestamp", "")},
        {"OriginOfCondition",
         {{"@odata.id", properties.value("OriginOfCondition", "")}}},
    };
}

} // namespace redfish
