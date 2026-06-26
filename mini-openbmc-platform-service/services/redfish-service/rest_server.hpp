#pragma once

#include "libs/common/logger.hpp"
#include "libs/dbus/dbus_client.hpp"
#include "services/redfish-service/event_log_controller.hpp"
#include "services/redfish-service/pcie_controller.hpp"
#include "services/redfish-service/sensor_controller.hpp"

#include <httplib.h>

#include <atomic>

namespace service {

class RestServer {
  public:
    RestServer();
    int run(const std::string& address = "127.0.0.1", int port = 8080);
    void stop();

  private:
    void registerRoutes();
    static void sendJson(httplib::Response& response, int status,
                         const nlohmann::json& body);
    static void sendStatusError(httplib::Response& response,
                                const common::Status& status);

    httplib::Server server_;
    dbus::DbusClient dbusClient_;
    SensorController sensorController_;
    PcieController pcieController_;
    EventLogController eventLogController_;
    common::JsonLogger logger_;
    std::atomic_bool stopped_{false};
};

} // namespace service
