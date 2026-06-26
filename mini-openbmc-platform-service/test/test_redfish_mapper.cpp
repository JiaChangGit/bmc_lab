#include "libs/redfish/redfish_mapper.hpp"

#include "libs/common/time_utils.hpp"

#include <gtest/gtest.h>

TEST(RedfishMapper, MapsSensorProperties) {
    sensor::SensorReading reading{
        "GPU0_Core_Temp", "GPU0 Core Temperature",
        sensor::SensorType::temperature, 72.5, "Cel", sensor::State::enabled,
        sensor::Health::ok, "PLDMType2Backend", "UDS-MCTP", "EID=8",
        "/xyz/openbmc_project/sensors/temperature/gpu0_core",
        common::iso8601Now(), 85.0, std::nullopt};
    const auto dbus = redfish::sensorToDbusProperties(reading);
    const auto json = redfish::sensorPropertiesToRedfish(dbus);
    EXPECT_EQ(json["Id"], "GPU0_Core_Temp");
    EXPECT_DOUBLE_EQ(json["Reading"], 72.5);
    EXPECT_EQ(json["ReadingUnits"], "Cel");
    EXPECT_EQ(json["Status"]["Health"], "OK");
    EXPECT_EQ(json["Oem"]["MiniOpenBMC"]["SourceBus"], "UDS-MCTP");
    EXPECT_TRUE(json["LowerThresholdCritical"].is_null());
}

TEST(RedfishMapper, MapsEventProperties) {
    sensor::EventRecord event{"1", "Critical", "threshold exceeded",
                              "2026-06-21T10:00:00Z",
                              "/redfish/v1/Chassis/GPU0/Sensors/GPU0_Core_Temp",
                              "GPU0_Core_Temp", false};
    const auto json = redfish::eventPropertiesToRedfish(
        redfish::eventToDbusProperties(event));
    EXPECT_EQ(json["Severity"], "Critical");
    EXPECT_EQ(json["Created"], "2026-06-21T10:00:00Z");
    EXPECT_EQ(json["OriginOfCondition"]["@odata.id"],
              event.originOfCondition);
}
