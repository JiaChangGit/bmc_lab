#include "services/redfish-service/sensor_controller.hpp"

#include "libs/dbus/dbus_names.hpp"
#include "libs/redfish/redfish_mapper.hpp"

namespace service {

SensorController::SensorController(dbus::DbusClient& client) : client_(client) {}

common::StatusOr<nlohmann::json> SensorController::collection() {
    auto objects = client_.listObjects();
    if (!objects.ok()) return objects.status();
    nlohmann::json members = nlohmann::json::array();
    for (const auto& [path, properties] : objects.value()) {
        (void)path;
        if (properties.value("Kind", "") == "Sensor") {
            members.push_back(redfish::sensorPropertiesToRedfish(properties));
        }
    }
    return nlohmann::json{
        {"@odata.type", "#SensorCollection.SensorCollection"},
        {"@odata.id", "/redfish/v1/Chassis/GPU0/Sensors"},
        {"Name", "GPU0 Sensor Collection"},
        {"Members@odata.count", members.size()},
        {"Members", std::move(members)}};
}

common::StatusOr<nlohmann::json>
SensorController::get(const std::string& sensorId) {
    auto path = dbus::objectPathForSensorId(sensorId);
    if (!path) {
        return common::Status::error(common::StatusCode::notFound,
                                     "unknown sensor ID");
    }
    auto object = client_.getObject(*path);
    if (!object.ok()) return object.status();
    return redfish::sensorPropertiesToRedfish(object.value());
}

} // namespace service
