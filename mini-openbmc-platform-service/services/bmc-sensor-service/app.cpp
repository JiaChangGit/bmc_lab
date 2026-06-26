#include "services/bmc-sensor-service/app.hpp"

namespace service {

SensorServiceApp::SensorServiceApp() : sensorManager_(dbusServer_) {}

common::Status SensorServiceApp::run() {
    auto status = dbusServer_.start();
    if (!status.ok()) return status;
    status = sensorManager_.start();
    if (!status.ok()) return status;
    while (!stopping_) {
        status = dbusServer_.process(std::chrono::milliseconds(100));
        if (!status.ok()) return status;
    }
    sensorManager_.stop();
    dbusServer_.stop();
    return common::Status::okStatus();
}

void SensorServiceApp::stop() { stopping_ = true; }

} // namespace service
