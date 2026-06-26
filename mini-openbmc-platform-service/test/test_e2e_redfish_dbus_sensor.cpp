#include "libs/redfish/redfish_mapper.hpp"

#include <gtest/gtest.h>

TEST(E2eMapping, DbusSnapshotProducesRedfishSensorShape) {
    nlohmann::json properties{
        {"Kind", "Sensor"},
        {"Id", "GPU0_Core_Temp"},
        {"Name", "GPU0 Core Temperature"},
        {"Reading", 72.5},
        {"Unit", "Cel"},
        {"State", "Enabled"},
        {"Health", "Critical"},
        {"UpperCritical", 85.0},
        {"LowerCritical", nullptr},
        {"SourceBackend", "PLDMType2Backend"},
        {"SourceBus", "UDS-MCTP"},
        {"SourceAddress", "EID=8"},
        {"ObjectPath", "/xyz/openbmc_project/sensors/temperature/gpu0_core"}};
    auto json = redfish::sensorPropertiesToRedfish(properties);
    EXPECT_EQ(json["@odata.type"], "#Sensor.v1_0_0.Sensor");
    EXPECT_EQ(json["Status"]["Health"], "Critical");
    EXPECT_TRUE(json.contains("Oem"));
    EXPECT_TRUE(json["Oem"].contains("MiniOpenBMC"));
}

TEST(E2eMapping, DbusEventSnapshotProducesRedfishLogShape) {
    nlohmann::json properties{
        {"Kind", "Event"},
        {"Id", "1"},
        {"Severity", "Critical"},
        {"Message", "GPU0 Core Temperature exceeded upper critical threshold"},
        {"Timestamp", "2026-06-21T10:00:00Z"},
        {"OriginOfCondition",
         "/redfish/v1/Chassis/GPU0/Sensors/GPU0_Core_Temp"},
        {"SensorId", "GPU0_Core_Temp"}};
    auto json = redfish::eventPropertiesToRedfish(properties);
    EXPECT_EQ(json["Id"], "1");
    EXPECT_EQ(json["Severity"], "Critical");
    EXPECT_EQ(json["OriginOfCondition"]["@odata.id"],
              properties["OriginOfCondition"]);
}
