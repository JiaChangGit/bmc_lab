#include "services/redfish-service/pcie_controller.hpp"

namespace service {
namespace {
constexpr const char* kPciePath =
    "/xyz/openbmc_project/inventory/system/chassis/GPU0/PCIeDevice0";

nlohmann::json toRedfish(const nlohmann::json& properties) {
    return {
        {"@odata.type", "#PCIeDevice.v1_10_0.PCIeDevice"},
        {"@odata.id", "/redfish/v1/Chassis/GPU0/PCIeDevices/PCIeDevice0"},
        {"Id", properties.value("Id", "PCIeDevice0")},
        {"Name", properties.value("Name", "GPU0 PCIe Device")},
        {"DeviceType", properties.value("DeviceType", "PCIeDevice")},
        {"Status",
         {{"State", properties.value("Present", false) ? "Enabled" : "Absent"},
          {"Health", properties.value("Health", "Unknown")}}},
        {"PCIeInterface",
         {{"MaxLanes", properties.value("LinkWidth", 0)},
          {"MaxPCIeType", properties.value("LinkSpeed", "unknown")}}},
        {"Oem",
         {{"MiniOpenBMC",
           {{"LinkState", properties.value("LinkState", "Unknown")},
            {"CorrectableErrorCount",
             properties.value("CorrectableErrorCount", 0ULL)},
            {"NonfatalErrorCount",
             properties.value("NonfatalErrorCount", 0ULL)}}}}}};
}

} // namespace

PcieController::PcieController(dbus::DbusClient& client) : client_(client) {}

common::StatusOr<nlohmann::json> PcieController::collection() {
    auto object = client_.getObject(kPciePath);
    nlohmann::json members = nlohmann::json::array();
    if (object.ok()) members.push_back(toRedfish(object.value()));
    return nlohmann::json{
        {"@odata.type", "#PCIeDeviceCollection.PCIeDeviceCollection"},
        {"@odata.id", "/redfish/v1/Chassis/GPU0/PCIeDevices"},
        {"Name", "GPU0 PCIe Device Collection"},
        {"Members@odata.count", members.size()},
        {"Members", std::move(members)}};
}

common::StatusOr<nlohmann::json>
PcieController::get(const std::string& deviceId) {
    if (deviceId != "PCIeDevice0") {
        return common::Status::error(common::StatusCode::notFound,
                                     "unknown PCIe device ID");
    }
    auto object = client_.getObject(kPciePath);
    if (!object.ok()) return object.status();
    return toRedfish(object.value());
}

} // namespace service
