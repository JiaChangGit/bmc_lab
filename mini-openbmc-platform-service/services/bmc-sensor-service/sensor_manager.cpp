#include "services/bmc-sensor-service/sensor_manager.hpp"

#include "libs/common/byte_buffer.hpp"
#include "libs/common/file_utils.hpp"
#include "libs/common/time_utils.hpp"
#include "libs/dbus/dbus_names.hpp"
#include "libs/hwmon/hwmon_sensor_backend.hpp"
#include "libs/mctp/mctp_packet.hpp"
#include "libs/pcie/mini_pcie_backend.hpp"
#include "libs/pcie/pci_sysfs_reader.hpp"
#include "libs/pldm/pldm_common.hpp"
#include "libs/pldm/pldm_type0.hpp"
#include "libs/pldm/pldm_type2.hpp"
#include "libs/redfish/redfish_mapper.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>

namespace service {
namespace {

sensor::SensorReading makeSensor(std::string id, std::string name,
                                 sensor::SensorType type, double value,
                                 std::string unit, std::string backend,
                                 std::string bus, std::string address,
                                 std::optional<double> upper,
                                 std::optional<double> lower) {
    const auto path = dbus::objectPathForSensorId(id).value_or(
        "/xyz/openbmc_project/sensors/unknown/" + id);
    return {std::move(id), std::move(name), type, value, std::move(unit),
            sensor::State::enabled, sensor::Health::ok, std::move(backend),
            std::move(bus), std::move(address), path, common::iso8601Now(),
            upper, lower};
}

bool isPcieFault(const std::string& fault) {
    static const std::set<std::string> values{
        "link_down", "link_degraded", "correctable_error_spike",
        "nonfatal_error", "telemetry_timeout", "over_temperature", "over_power"};
    return values.contains(fault);
}

bool isHwmonFault(const std::string& fault) {
    static const std::set<std::string> values{
        "read_timeout", "device_disappeared", "stuck_value", "out_of_range",
        "invalid_reading"};
    return values.contains(fault);
}

bool isPldmFault(const std::string& fault) {
    static const std::set<std::string> values{
        "packet_loss", "out_of_order_packet", "sequence_mismatch",
        "timeout_before_eom", "unsupported_command", "bad_completion_code",
        "malformed_response", "sensor_unavailable"};
    return values.contains(fault);
}

struct PldmSensorBinding {
    std::uint8_t eid;
    std::uint16_t sensorId;
    const char* localId;
    const char* name;
    sensor::SensorType type;
    double initialValue;
    const char* unit;
    std::optional<double> upperCritical;
    std::optional<double> lowerCritical;
};

const std::array<PldmSensorBinding, 8>& pldmSensorBindings() {
    static const std::array<PldmSensorBinding, 8> bindings{{
        {8, 1, "GPU0_Core_Temp", "GPU0 Core Temperature",
         sensor::SensorType::temperature, 65.0, "Cel", 85.0, std::nullopt},
        {8, 2, "GPU0_Power", "GPU0 Power", sensor::SensorType::power, 250.0,
         "W", 320.0, std::nullopt},
        {8, 3, "GPU0_PCIe_Correctable_Errors",
         "PCIe Correctable Error Count", sensor::SensorType::count, 0.0,
         "Count", 100.0, std::nullopt},
        {8, 4, "GPU0_PCIe_Link_Status", "PCIe Link Status",
         sensor::SensorType::link, 1.0, "State", std::nullopt, 0.5},
        {9, 101, "NIC0_Temp", "NIC0 Temperature",
         sensor::SensorType::temperature, 48.0, "Cel", 80.0, std::nullopt},
        {9, 102, "NIC0_Link_Status", "NIC0 Link Status",
         sensor::SensorType::network, 1.0, "State", std::nullopt, 0.5},
        {9, 103, "NIC0_Correctable_Errors",
         "NIC0 Correctable Error Count", sensor::SensorType::count, 0.0,
         "Count", 100.0, std::nullopt},
        {9, 104, "NIC0_Packet_Errors", "NIC0 Packet Error Count",
         sensor::SensorType::count, 0.0, "Count", 1000.0, std::nullopt},
    }};
    return bindings;
}

const PldmSensorBinding* findPldmSensor(std::uint8_t eid,
                                       std::uint16_t sensorId) {
    const auto& bindings = pldmSensorBindings();
    const auto iterator = std::find_if(
        bindings.begin(), bindings.end(),
        [eid, sensorId](const auto& binding) {
            return binding.eid == eid && binding.sensorId == sensorId;
        });
    return iterator == bindings.end() ? nullptr : &*iterator;
}

} // namespace

SensorManager::SensorManager(dbus::DbusServer& dbusServer,
                             std::chrono::milliseconds pollingInterval)
    : dbusServer_(dbusServer), pollingInterval_(pollingInterval),
      logger_("runtime/logs/mini-openbmc-service.jsonl"),
      mctpClient_("runtime/sockets/mctp_endpoint.sock") {}

SensorManager::~SensorManager() { stop(); }

common::Status SensorManager::start() {
    initializeObjects();
    discoverPldm();
    dbusServer_.setFaultHandler(
        [this](const auto& target, const auto& fault, bool enabled) {
            return injectFault(target, fault, enabled);
        });
    stopping_ = false;
    pollingThread_ = std::thread(&SensorManager::pollingLoop, this);
    return common::Status::okStatus();
}

void SensorManager::discoverPldm() {
    discoverPldmEndpoint(8);
    discoverPldmEndpoint(9);
}

void SensorManager::discoverPldmEndpoint(std::uint8_t eid) {
    const auto getTid = requestPldm(
        eid, pldm::makeRequest(
                 pldm::Type::base,
                 static_cast<std::uint8_t>(pldm::BaseCommand::getTid)));
    if (!getTid.ok()) {
        logger_.log("WARN", "bmc-sensor-service",
                    "PLDM endpoint discovery was skipped",
                    {{"service", "SensorManager"},
                     {"eid", eid},
                     {"errorCode", static_cast<int>(getTid.status().code())}});
        return;
    }
    const auto tid = pldm::parseGetTidResponse(getTid.value());
    if (!tid.ok()) return;

    const auto types = requestPldm(
        eid, pldm::makeRequest(
                 pldm::Type::base,
                 static_cast<std::uint8_t>(pldm::BaseCommand::getPldmTypes)));
    const auto commands = requestPldm(
        eid, pldm::makeRequest(
                 pldm::Type::base,
                 static_cast<std::uint8_t>(pldm::BaseCommand::getPldmCommands),
                 {static_cast<std::uint8_t>(pldm::Type::platform)}));
    if (!types.ok() || !commands.ok() ||
        types.value().completionCode != pldm::CompletionCode::success ||
        commands.value().completionCode != pldm::CompletionCode::success) {
        return;
    }

    const auto repositoryInfo = requestPldm(
        eid, pldm::makeRequest(
                 pldm::Type::platform,
                 static_cast<std::uint8_t>(
                     pldm::PlatformCommand::getPdrRepositoryInfo)));
    if (!repositoryInfo.ok() ||
        repositoryInfo.value().completionCode != pldm::CompletionCode::success) {
        return;
    }
    common::ByteReader infoReader(repositoryInfo.value().payload);
    const auto recordCount = infoReader.readLe32();
    if (!recordCount.ok()) return;

    std::uint32_t handle = 0;
    for (std::uint32_t index = 0; index < recordCount.value(); ++index) {
        std::vector<std::uint8_t> requestPayload;
        common::appendLe32(requestPayload, handle);
        const auto getPdr = requestPldm(
            eid, pldm::makeRequest(
                     pldm::Type::platform,
                     static_cast<std::uint8_t>(pldm::PlatformCommand::getPdr),
                     std::move(requestPayload)));
        if (!getPdr.ok() ||
            getPdr.value().completionCode != pldm::CompletionCode::success ||
            getPdr.value().payload.size() < 4) {
            break;
        }
        common::ByteReader pdrReader(getPdr.value().payload);
        const auto nextHandle = pdrReader.readLe32();
        const auto rawPdr = pdrReader.readBytes(pdrReader.remaining());
        if (!nextHandle.ok() || !rawPdr.ok()) break;
        const auto pdr = pldm::decodePdr(rawPdr.value());
        if (!pdr.ok()) break;
        {
            std::lock_guard lock(mutex_);
            const auto* binding = findPldmSensor(eid, pdr.value().sensorId);
            const auto sensor =
                binding ? sensors_.find(binding->localId) : sensors_.end();
            if (sensor != sensors_.end()) {
                sensor->second.name = pdr.value().name;
                sensor->second.unit = pdr.value().unit;
                if (sensor->second.type == sensor::SensorType::link ||
                    sensor->second.type == sensor::SensorType::network) {
                    sensor->second.upperCritical = std::nullopt;
                    sensor->second.lowerCritical = pdr.value().lowerCritical;
                } else {
                    sensor->second.upperCritical = pdr.value().upperCritical;
                    sensor->second.lowerCritical = std::nullopt;
                }
            }
        }
        logger_.log("INFO", "bmc-sensor-service", "PLDM numeric sensor PDR discovered",
                    {{"service", "SensorManager"},
                     {"eid", eid},
                     {"sensorId", pdr.value().sensorId},
                     {"objectPath", pdr.value().name}});
        handle = nextHandle.value();
        if (handle == 0) break;
    }
    (void)requestPldm(
        eid, pldm::makeRequest(
                 pldm::Type::platform,
                 static_cast<std::uint8_t>(
                     pldm::PlatformCommand::setEventReceiver),
                 {1}));
    logger_.log("INFO", "bmc-sensor-service", "PLDM endpoint discovery completed",
                {{"service", "SensorManager"},
                 {"eid", eid},
                 {"state", "TID=" + std::to_string(tid.value())}});
}

void SensorManager::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    condition_.notify_all();
    if (pollingThread_.joinable()) pollingThread_.join();
}

void SensorManager::initializeObjects() {
    std::lock_guard lock(mutex_);
    for (const auto& binding : pldmSensorBindings()) {
        sensors_.emplace(
            binding.localId,
            makeSensor(binding.localId, binding.name, binding.type,
                       binding.initialValue, binding.unit, "PLDMType2Backend",
                       "UDS-MCTP", "EID=" + std::to_string(binding.eid),
                       binding.upperCritical, binding.lowerCritical));
    }
    sensors_.emplace("Fan0_Tach",
                     makeSensor("Fan0_Tach", "Fan0 Tach",
                                sensor::SensorType::fanTach, 8000.0, "RPM",
                                "HwmonBackend", "sysfs", "mini_i2c_hwmon",
                                std::nullopt, 3000.0));
    sensors_.emplace("CPU_Board_Temp",
                     makeSensor("CPU_Board_Temp", "CPU Board Temperature",
                                sensor::SensorType::temperature, 42.0, "Cel",
                                "HwmonBackend", "sysfs", "mini_i2c_hwmon",
                                90.0, std::nullopt));
    sensors_.emplace("Board_Voltage",
                     makeSensor("Board_Voltage", "Board Voltage",
                                sensor::SensorType::voltage, 12.0, "V",
                                "HwmonBackend", "sysfs", "mini_i2c_hwmon",
                                13.2, 10.8));

    for (auto& [id, reading] : sensors_) {
        (void)id;
        publishSensor(reading);
    }
    dbusServer_.upsertObject(
        "/xyz/openbmc_project/inventory/system/chassis/GPU0",
        {{"Kind", "Inventory"}, {"Id", "GPU0"}, {"Name", "GPU0"},
         {"Present", true}, {"PrettyName", "Mini GPU"}, {"DeviceType", "GPU"},
         {"Health", "OK"}, {"HealthRollup", "OK"}, {"LastError", ""}});
    dbusServer_.upsertObject(
        "/xyz/openbmc_project/inventory/system/chassis/NIC0",
        {{"Kind", "Inventory"}, {"Id", "NIC0"}, {"Name", "NIC0"},
         {"Present", true}, {"PrettyName", "Mini NIC"}, {"DeviceType", "NIC"},
         {"Health", "OK"}, {"HealthRollup", "OK"}, {"LastError", ""}});
}

void SensorManager::pollingLoop() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        lock.unlock();
        pollOnce();
        lock.lock();
        condition_.wait_for(lock, pollingInterval_, [this] { return stopping_; });
    }
}

void SensorManager::pollOnce() {
    pollPldm();
    pollKernelTelemetry();
    std::vector<sensor::SensorReading> snapshots;
    std::vector<sensor::EventRecord> newEvents;
    {
        std::lock_guard lock(mutex_);
        for (auto& [id, reading] : sensors_) {
            const auto fault = injectedFaults_.find(id);
            if (fault != injectedFaults_.end()) {
                if (fault->second == "out_of_range") {
                    if (reading.upperCritical) {
                        reading.reading = *reading.upperCritical + 10.0;
                    } else if (reading.lowerCritical) {
                        reading.reading = *reading.lowerCritical - 10.0;
                    }
                    reading.state = sensor::State::enabled;
                } else if (fault->second == "device_disappeared" ||
                           fault->second == "sensor_unavailable") {
                    reading.state = sensor::State::unavailable;
                    reading.health = sensor::Health::unknown;
                }
            }
            sensor::Threshold threshold{
                reading.upperCritical, reading.lowerCritical,
                reading.type == sensor::SensorType::temperature ? 2.0 : 0.1,
                reading.id == "Fan0_Tach" ? sensor::Health::warning
                                          : sensor::Health::critical};
            if (reading.state == sensor::State::enabled) {
                auto result = thresholdEngine_.evaluate(reading, threshold);
                reading.health = result.health;
                if (result.event) {
                    events_.push_back(*result.event);
                    newEvents.push_back(*result.event);
                }
            } else {
                reading.health = sensor::Health::unknown;
            }
            reading.lastUpdated = common::iso8601Now();
            snapshots.push_back(reading);
        }
    }
    for (const auto& event : newEvents) publishEvent(event);
    for (auto& reading : snapshots) {
        publishSensor(reading);
        logger_.log("INFO", "bmc-sensor-service", "Sensor reading updated",
                    {{"service", "SensorManager"},
                     {"objectPath", reading.objectPath},
                     {"sensorId", reading.id},
                     {"backend", reading.sourceBackend},
                     {"bus", reading.sourceBus},
                     {"state", sensor::toString(reading.state)}});
    }
}

void SensorManager::pollPldm() {
    for (const auto& binding : pldmSensorBindings()) {
        std::vector<std::uint8_t> payload;
        common::appendLe16(payload, binding.sensorId);
        auto response = requestPldm(
            binding.eid,
            pldm::makeRequest(
                pldm::Type::platform,
                static_cast<std::uint8_t>(
                    pldm::PlatformCommand::getSensorReading),
                std::move(payload)));
        auto reading = response.ok()
                           ? pldm::parseGetSensorReadingResponse(response.value())
                           : common::StatusOr<pldm::NumericReading>(
                                 response.status());
        std::lock_guard lock(mutex_);
        const auto iterator = sensors_.find(binding.localId);
        if (iterator == sensors_.end()) continue;
        if (!reading.ok() || !reading.value().available) {
            iterator->second.state = sensor::State::unavailable;
            iterator->second.health = sensor::Health::unknown;
            continue;
        }
        iterator->second.reading = reading.value().value;
        iterator->second.state = sensor::State::enabled;
    }
}

void SensorManager::pollKernelTelemetry() {
    pcie::MiniPcieBackend pcieBackend;
    auto telemetry = pcieBackend.readTelemetry();
    if (telemetry.ok()) {
        dbusServer_.upsertObject(
            "/xyz/openbmc_project/inventory/system/chassis/GPU0/PCIeDevice0",
            {{"Kind", "Inventory"}, {"Id", "PCIeDevice0"},
             {"Name", telemetry.value().deviceId}, {"Present", true},
             {"PrettyName", "GPU0 PCIe Telemetry Device"},
             {"DeviceType", "PCIeDevice"}, {"Health", telemetry.value().health},
             {"HealthRollup", telemetry.value().health}, {"LastError", ""},
             {"LinkWidth", telemetry.value().linkWidth},
             {"LinkSpeed", telemetry.value().linkSpeed},
             {"LinkState", telemetry.value().linkState},
             {"CorrectableErrorCount", telemetry.value().correctableErrors},
             {"NonfatalErrorCount", telemetry.value().nonfatalErrors}});
    } else {
        pcie::PciSysfsReader reader;
        auto devices = reader.scan();
        if (devices.ok() && !devices.value().empty()) {
            const auto& first = devices.value().front();
            dbusServer_.upsertObject(
                "/xyz/openbmc_project/inventory/system/chassis/GPU0/PCIeDevice0",
                {{"Kind", "Inventory"}, {"Id", "PCIeDevice0"},
                 {"Name", first.bdf}, {"Present", true},
                 {"PrettyName", "Host PCIe Device"}, {"DeviceType", "PCIeDevice"},
                 {"Health", "OK"}, {"HealthRollup", "OK"}, {"LastError", ""},
                 {"LinkWidth", first.currentLinkWidth.value_or(0)},
                 {"LinkSpeed", first.currentLinkSpeed.value_or("unknown")},
                 {"LinkState", "Detected"}});
        }
    }

    auto hwmonPath = hwmon::HwmonSensorBackend::discover();
    if (!hwmonPath.ok()) {
        std::lock_guard lock(mutex_);
        for (const auto* id :
             {"CPU_Board_Temp", "Board_Voltage", "Fan0_Tach"}) {
            sensors_[id].state = sensor::State::unavailable;
            sensors_[id].health = sensor::Health::unknown;
        }
        return;
    }
    hwmon::HwmonSensorBackend hwmon(hwmonPath.value());
    auto readings = hwmon.read();
    if (!readings.ok()) {
        std::lock_guard lock(mutex_);
        for (const auto* id :
             {"CPU_Board_Temp", "Board_Voltage", "Fan0_Tach"}) {
            sensors_[id].state = sensor::State::unavailable;
            sensors_[id].health = sensor::Health::unknown;
        }
        return;
    }
    std::lock_guard lock(mutex_);
    sensors_["CPU_Board_Temp"].reading =
        static_cast<double>(readings.value().temperatureMillic) / 1000.0;
    sensors_["Board_Voltage"].reading =
        static_cast<double>(readings.value().voltageMillivolt) / 1000.0;
    sensors_["Fan0_Tach"].reading = static_cast<double>(readings.value().fanRpm);
    sensors_["CPU_Board_Temp"].state = sensor::State::enabled;
    sensors_["Board_Voltage"].state = sensor::State::enabled;
    sensors_["Fan0_Tach"].state = sensor::State::enabled;
}

void SensorManager::publishSensor(sensor::SensorReading& reading) {
    (void)dbusServer_.upsertObject(reading.objectPath,
                                   redfish::sensorToDbusProperties(reading));
}

void SensorManager::publishEvent(const sensor::EventRecord& event) {
    const auto path = "/xyz/openbmc_project/logging/entry/" + event.id;
    (void)dbusServer_.upsertObject(path, redfish::eventToDbusProperties(event));
    logger_.log("WARN", "bmc-sensor-service", event.message,
                {{"service", "ThresholdEventEngine"},
                 {"objectPath", path},
                 {"sensorId", event.sensorId},
                 {"state", event.recovery ? "Recovered" : "Asserted"}});
}

common::Status SensorManager::injectFault(const std::string& target,
                                          const std::string& fault,
                                          bool enabled) {
    {
        std::lock_guard lock(mutex_);
        if (!sensors_.contains(target) && target != "PCIeDevice0") {
            return common::Status::error(common::StatusCode::invalidArgument,
                                         "unknown fault target: " + target);
        }
    }
    const std::string appliedFault = enabled ? fault : "none";
    if (isPcieFault(fault)) {
        auto parsed = pcie::pcieFaultFromString(appliedFault);
        if (!parsed.ok()) return parsed.status();
        pcie::MiniPcieBackend backend;
        const auto status = backend.setFault(parsed.value());
        if (!status.ok() && status.code() != common::StatusCode::notFound) {
            return status;
        }
    } else if (isHwmonFault(fault)) {
        auto path = hwmon::HwmonSensorBackend::discover();
        if (path.ok()) {
            hwmon::HwmonSensorBackend backend(path.value());
            auto status = backend.setFault(appliedFault);
            if (!status.ok()) return status;
        }
    } else if (isPldmFault(fault)) {
        auto status = setEndpointFault(appliedFault);
        if (!status.ok()) return status;
    } else if (fault != "out_of_range") {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "unsupported fault: " + fault);
    }
    {
        std::lock_guard lock(mutex_);
        if (enabled) {
            injectedFaults_[target] = fault;
        } else {
            injectedFaults_.erase(target);
        }
    }
    condition_.notify_all();
    logger_.log("INFO", "bmc-sensor-service", "Fault state changed",
                {{"service", "SensorManager"},
                 {"sensorId", target},
                 {"state", appliedFault}});
    return common::Status::okStatus();
}

common::Status SensorManager::setEndpointFault(const std::string& fault) {
    std::vector<std::uint8_t> payload(fault.begin(), fault.end());
    auto response = requestPldm(8, pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(pldm::PlatformCommand::setFault),
        std::move(payload)));
    if (!response.ok()) return response.status();
    if (response.value().completionCode != pldm::CompletionCode::success) {
        return common::Status::error(common::StatusCode::ioError,
                                     "endpoint rejected fault injection");
    }
    return common::Status::okStatus();
}

common::StatusOr<pldm::Message>
SensorManager::requestPldm(std::uint8_t eid, const pldm::Message& request) {
    auto encoded = pldm::encode(request);
    if (!encoded.ok()) return encoded.status();
    const auto fragmentCount =
        std::max<std::size_t>(
            1, (encoded.value().size() + mctp::kPayloadMtu - 1) /
                   mctp::kPayloadMtu);
    logger_.log("DEBUG", "bmc-sensor-service",
                "MCTP request fragmented",
                {{"service", "UdsMctpClient"},
                 {"backend", "PLDMType2Backend"},
                 {"bus", "UDS-MCTP"},
                 {"eid", eid},
                 {"pldmType", static_cast<int>(request.header.type)},
                 {"pldmCommand", request.header.command},
                 {"state", "fragments=" + std::to_string(fragmentCount)}});
    std::lock_guard mctpLock(mctpMutex_);
    auto response = mctpClient_.request(encoded.value(), eid);
    if (!response.ok()) {
        logger_.log("WARN", "bmc-sensor-service",
                    "MCTP request failed",
                    {{"service", "UdsMctpClient"},
                     {"backend", "PLDMType2Backend"},
                     {"bus", "UDS-MCTP"},
                     {"eid", eid},
                     {"pldmType", static_cast<int>(request.header.type)},
                     {"pldmCommand", request.header.command},
                     {"errorCode",
                      static_cast<int>(response.status().code())}});
        return response.status();
    }
    auto decoded = pldm::decode(response.value());
    if (!decoded.ok()) return decoded.status();
    if (decoded.value().header.instanceId != request.header.instanceId ||
        decoded.value().header.command != request.header.command ||
        decoded.value().header.type != request.header.type) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "PLDM response did not match request");
    }
    logger_.log("DEBUG", "bmc-sensor-service",
                "MCTP response reassembled",
                {{"service", "UdsMctpClient"},
                 {"backend", "PLDMType2Backend"},
                 {"bus", "UDS-MCTP"},
                 {"eid", eid},
                 {"pldmType", static_cast<int>(request.header.type)},
                 {"pldmCommand", request.header.command},
                 {"state", "bytes=" + std::to_string(response.value().size())}});
    return decoded.value();
}

} // namespace service
