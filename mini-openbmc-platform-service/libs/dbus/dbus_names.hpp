#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace dbus {

inline constexpr const char* kServiceName =
    "xyz.openbmc_project.MiniBMC.SensorService";
inline constexpr const char* kManagerPath = "/xyz/openbmc_project/MiniBMC";
inline constexpr const char* kManagerInterface =
    "xyz.openbmc_project.MiniBMC.Manager";
inline constexpr const char* kSensorInterface =
    "xyz.openbmc_project.Sensor.Value";
inline constexpr const char* kThresholdInterface =
    "xyz.openbmc_project.Sensor.Threshold.Critical";
inline constexpr const char* kHealthInterface =
    "xyz.openbmc_project.State.Decorator.Health";
inline constexpr const char* kInventoryInterface =
    "xyz.openbmc_project.Inventory.Item";
inline constexpr const char* kLoggingInterface =
    "xyz.openbmc_project.Logging.Entry";

inline const std::unordered_map<std::string, std::string>& sensorPaths() {
    static const std::unordered_map<std::string, std::string> paths{
        {"GPU0_Core_Temp",
         "/xyz/openbmc_project/sensors/temperature/gpu0_core"},
        {"GPU0_Power", "/xyz/openbmc_project/sensors/power/gpu0_power"},
        {"GPU0_PCIe_Correctable_Errors",
         "/xyz/openbmc_project/sensors/count/gpu0_pcie_correctable_errors"},
        {"GPU0_PCIe_Link_Status",
         "/xyz/openbmc_project/sensors/network/gpu0_pcie_link_status"},
        {"NIC0_Temp", "/xyz/openbmc_project/sensors/temperature/nic0"},
        {"NIC0_Link_Status",
         "/xyz/openbmc_project/sensors/network/nic0_link_status"},
        {"NIC0_Correctable_Errors",
         "/xyz/openbmc_project/sensors/count/nic0_correctable_errors"},
        {"NIC0_Packet_Errors",
         "/xyz/openbmc_project/sensors/count/nic0_packet_errors"},
        {"Fan0_Tach", "/xyz/openbmc_project/sensors/fan_tach/fan0"},
        {"CPU_Board_Temp",
         "/xyz/openbmc_project/sensors/temperature/cpu_board_temp"},
        {"Board_Voltage",
         "/xyz/openbmc_project/sensors/voltage/board_voltage"},
    };
    return paths;
}

inline std::optional<std::string> objectPathForSensorId(const std::string& id) {
    const auto iterator = sensorPaths().find(id);
    return iterator == sensorPaths().end()
               ? std::nullopt
               : std::optional<std::string>(iterator->second);
}

inline std::optional<std::string> sensorIdForObjectPath(const std::string& path) {
    for (const auto& [id, objectPath] : sensorPaths()) {
        if (objectPath == path) return id;
    }
    return std::nullopt;
}

inline std::string dbusPropertyForRedfish(const std::string& property) {
    static const std::unordered_map<std::string, std::string> mapping{
        {"ReadingUnits", "Unit"},
        {"UpperThresholdCritical", "UpperCritical"},
        {"LowerThresholdCritical", "LowerCritical"},
        {"Created", "Timestamp"},
    };
    const auto iterator = mapping.find(property);
    return iterator == mapping.end() ? property : iterator->second;
}

} // namespace dbus
