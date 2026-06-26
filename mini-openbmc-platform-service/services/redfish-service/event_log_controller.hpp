#pragma once

#include "libs/dbus/dbus_client.hpp"

#include <nlohmann/json.hpp>

namespace service {

class EventLogController {
  public:
    explicit EventLogController(dbus::DbusClient& client);
    common::StatusOr<nlohmann::json> collection();

  private:
    dbus::DbusClient& client_;
};

} // namespace service
