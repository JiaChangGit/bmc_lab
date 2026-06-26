#pragma once

#include "libs/dbus/dbus_client.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace service {

class SensorController {
  public:
    explicit SensorController(dbus::DbusClient& client);
    common::StatusOr<nlohmann::json> collection();
    common::StatusOr<nlohmann::json> get(const std::string& sensorId);

  private:
    dbus::DbusClient& client_;
};

} // namespace service
