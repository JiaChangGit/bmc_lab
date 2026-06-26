#pragma once

#include "libs/dbus/dbus_server.hpp"
#include "services/bmc-sensor-service/sensor_manager.hpp"

#include <atomic>

namespace service {

class SensorServiceApp {
  public:
    SensorServiceApp();
    common::Status run();
    void stop();

  private:
    std::atomic_bool stopping_{false};
    dbus::DbusServer dbusServer_;
    SensorManager sensorManager_;
};

} // namespace service
