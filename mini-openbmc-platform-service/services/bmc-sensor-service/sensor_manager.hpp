#pragma once

#include "libs/common/logger.hpp"
#include "libs/dbus/dbus_server.hpp"
#include "libs/mctp/uds_mctp_transport.hpp"
#include "libs/pldm/pldm_common.hpp"
#include "libs/sensor/sensor_reading.hpp"
#include "libs/sensor/threshold_event_engine.hpp"

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace service {

class SensorManager {
  public:
    explicit SensorManager(dbus::DbusServer& dbusServer,
                           std::chrono::milliseconds pollingInterval =
                               std::chrono::milliseconds(1000));
    ~SensorManager();
    common::Status start();
    void stop();
    common::Status injectFault(const std::string& target,
                               const std::string& fault, bool enabled);

  private:
    void initializeObjects();
    void discoverPldm();
    void discoverPldmEndpoint(std::uint8_t eid);
    void pollingLoop();
    void pollOnce();
    void pollPldm();
    void pollKernelTelemetry();
    void publishSensor(sensor::SensorReading& reading);
    void publishEvent(const sensor::EventRecord& event);
    common::Status setEndpointFault(const std::string& fault);
    common::StatusOr<pldm::Message> requestPldm(std::uint8_t eid,
                                                const pldm::Message& request);

    dbus::DbusServer& dbusServer_;
    std::chrono::milliseconds pollingInterval_;
    common::JsonLogger logger_;
    mctp::UdsMctpClient mctpClient_;
    sensor::ThresholdEventEngine thresholdEngine_;
    std::map<std::string, sensor::SensorReading> sensors_;
    std::vector<sensor::EventRecord> events_;
    std::map<std::string, std::string> injectedFaults_;
    std::mutex mctpMutex_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread pollingThread_;
    bool stopping_{};
};

} // namespace service
