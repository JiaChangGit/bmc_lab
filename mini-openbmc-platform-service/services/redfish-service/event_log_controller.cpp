#include "services/redfish-service/event_log_controller.hpp"

#include "libs/redfish/redfish_mapper.hpp"

namespace service {

EventLogController::EventLogController(dbus::DbusClient& client) : client_(client) {}

common::StatusOr<nlohmann::json> EventLogController::collection() {
    auto objects = client_.listObjects();
    if (!objects.ok()) return objects.status();
    nlohmann::json members = nlohmann::json::array();
    for (const auto& [path, properties] : objects.value()) {
        (void)path;
        if (properties.value("Kind", "") == "Event") {
            members.push_back(redfish::eventPropertiesToRedfish(properties));
        }
    }
    return nlohmann::json{
        {"@odata.type", "#LogEntryCollection.LogEntryCollection"},
        {"@odata.id",
         "/redfish/v1/Systems/System0/LogServices/EventLog/Entries"},
        {"Name", "MiniBMC Event Log Entries"},
        {"Members@odata.count", members.size()},
        {"Members", std::move(members)}};
}

} // namespace service
