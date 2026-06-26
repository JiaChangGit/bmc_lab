#include "libs/dbus/dbus_names.hpp"

#include <gtest/gtest.h>

TEST(DbusMapping, SensorIdMapsToObjectPath) {
    auto path = dbus::objectPathForSensorId("GPU0_Core_Temp");
    ASSERT_TRUE(path);
    EXPECT_EQ(*path, "/xyz/openbmc_project/sensors/temperature/gpu0_core");
    EXPECT_EQ(dbus::sensorIdForObjectPath(*path), "GPU0_Core_Temp");
}

TEST(DbusMapping, UnknownSensorDoesNotMap) {
    EXPECT_FALSE(dbus::objectPathForSensorId("missing"));
}

TEST(DbusMapping, AllPldmSensorIdsMapToStableObjectPaths) {
    EXPECT_EQ(*dbus::objectPathForSensorId("GPU0_PCIe_Correctable_Errors"),
              "/xyz/openbmc_project/sensors/count/"
              "gpu0_pcie_correctable_errors");
    EXPECT_EQ(*dbus::objectPathForSensorId("NIC0_Temp"),
              "/xyz/openbmc_project/sensors/temperature/nic0");
    EXPECT_EQ(*dbus::objectPathForSensorId("NIC0_Packet_Errors"),
              "/xyz/openbmc_project/sensors/count/nic0_packet_errors");
}

TEST(DbusMapping, RedfishPropertyNamesMapToDbusNames) {
    EXPECT_EQ(dbus::dbusPropertyForRedfish("ReadingUnits"), "Unit");
    EXPECT_EQ(dbus::dbusPropertyForRedfish("Created"), "Timestamp");
    EXPECT_EQ(dbus::dbusPropertyForRedfish("Name"), "Name");
}
