#include "dbus/DbusBridge.hpp"

#include "services/ManagementService.hpp"

#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace
{

constexpr const char* serviceName = "xyz.openbmc_project.AIServer";
constexpr const char* serverPath = "/xyz/openbmc_project/ai/server";
constexpr const char* powerPath = "/xyz/openbmc_project/ai/power";
constexpr const char* eventPath = "/xyz/openbmc_project/ai/events";
constexpr const char* sensorBasePath = "/xyz/openbmc_project/ai/sensors";
constexpr const char* serverInterface = "xyz.openbmc_project.AIServer.Server";
constexpr const char* powerInterface = "xyz.openbmc_project.AIServer.Power";
constexpr const char* eventInterface = "xyz.openbmc_project.AIServer.EventLog";
constexpr const char* sensorInterface = "xyz.openbmc_project.AIServer.Sensor";

const sd_bus_vtable serverVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("SystemPowerBudgetWatts", "i", openbmc::dbus::DbusBridge::serverPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("PowerCapActive", "b", openbmc::dbus::DbusBridge::serverPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("FirmwareState", "s", openbmc::dbus::DbusBridge::serverPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("BusMode", "s", openbmc::dbus::DbusBridge::serverPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable powerVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("TotalPower", "d", openbmc::dbus::DbusBridge::powerPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("TotalGpuPower", "d", openbmc::dbus::DbusBridge::powerPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("TotalFanPower", "d", openbmc::dbus::DbusBridge::powerPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("TotalPsuPower", "d", openbmc::dbus::DbusBridge::powerPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("TotalNvmePower", "d", openbmc::dbus::DbusBridge::powerPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("BudgetExceeded", "b", openbmc::dbus::DbusBridge::powerPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable eventVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("EntryCount", "t", openbmc::dbus::DbusBridge::eventPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("LastEventId", "s", openbmc::dbus::DbusBridge::eventPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_SIGNAL("EventGenerated", "sssss", 0),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable gpuSensorVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Temperature", "d", openbmc::dbus::DbusBridge::sensorPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Power", "d", openbmc::dbus::DbusBridge::sensorPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Health", "s", openbmc::dbus::DbusBridge::sensorPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Throttled", "b", openbmc::dbus::DbusBridge::sensorPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

const sd_bus_vtable fanSensorVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Rpm", "i", openbmc::dbus::DbusBridge::sensorPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Pwm", "i", openbmc::dbus::DbusBridge::sensorPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Health", "s", openbmc::dbus::DbusBridge::sensorPropertyGetter, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END,
};

} // 匿名命名空間

namespace openbmc::dbus
{

DbusBridge::DbusBridge(services::ManagementService& managementService) : managementService_(managementService) {}

DbusBridge::~DbusBridge()
{
    stop();
}

void DbusBridge::start()
{
    if (running_.exchange(true))
    {
        return;
    }

    try
    {
        connectBus();
        registerObjects();
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(busMutex_);
        for (auto* slot : slots_)
        {
            sd_bus_slot_unref(slot);
        }
        slots_.clear();
        contexts_.clear();
        if (bus_ != nullptr)
        {
            sd_bus_unref(bus_);
            bus_ = nullptr;
        }
        running_ = false;
        throw;
    }

    workerThread_ = std::thread([this]() { processLoop(); });
    spdlog::info("D-Bus bridge started on {} bus", busMode_);
}

void DbusBridge::stop()
{
    if (!running_.exchange(false) && bus_ == nullptr)
    {
        return;
    }

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    std::lock_guard<std::mutex> lock(busMutex_);
    for (auto* slot : slots_)
    {
        sd_bus_slot_unref(slot);
    }
    slots_.clear();
    contexts_.clear();

    if (bus_ != nullptr)
    {
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
}

void DbusBridge::emitEventGenerated(const common::EventRecord& eventRecord)
{
    std::lock_guard<std::mutex> lock(busMutex_);
    if (bus_ == nullptr)
    {
        return;
    }

    sd_bus_emit_signal(
        bus_, eventPath, eventInterface, "EventGenerated", "sssss", eventRecord.timestamp.c_str(),
        eventRecord.severity.c_str(), eventRecord.component.c_str(), eventRecord.message.c_str(),
        eventRecord.eventId.c_str());

    sd_bus_emit_properties_changed(bus_, eventPath, eventInterface, "EntryCount", "LastEventId", nullptr);
}

void DbusBridge::emitAllPropertiesChanged()
{
    emitServerPropertiesChanged();

    std::lock_guard<std::mutex> lock(busMutex_);
    if (bus_ == nullptr)
    {
        return;
    }

    sd_bus_emit_properties_changed(
        bus_, powerPath, powerInterface, "TotalPower", "TotalGpuPower", "TotalFanPower", "TotalPsuPower",
        "TotalNvmePower", "BudgetExceeded", nullptr);
    sd_bus_emit_properties_changed(bus_, eventPath, eventInterface, "EntryCount", "LastEventId", nullptr);

    const auto platform = managementService_.getPlatformSnapshot();
    for (std::size_t index = 0; index < platform.hardware.gpus.size(); ++index)
    {
        const std::string path = std::string(sensorBasePath) + "/gpu" + std::to_string(index);
        sd_bus_emit_properties_changed(bus_, path.c_str(), sensorInterface, "Temperature", "Power", "Health",
                                       "Throttled", nullptr);
    }

    for (std::size_t index = 0; index < platform.hardware.fans.size(); ++index)
    {
        const std::string path = std::string(sensorBasePath) + "/fan" + std::to_string(index);
        sd_bus_emit_properties_changed(bus_, path.c_str(), sensorInterface, "Rpm", "Pwm", "Health", nullptr);
    }
}

void DbusBridge::emitServerPropertiesChanged()
{
    std::lock_guard<std::mutex> lock(busMutex_);
    if (bus_ == nullptr)
    {
        return;
    }

    sd_bus_emit_properties_changed(
        bus_, serverPath, serverInterface, "SystemPowerBudgetWatts", "PowerCapActive", "FirmwareState", nullptr);
}

std::string DbusBridge::busMode() const
{
    return busMode_;
}

void DbusBridge::connectBus()
{
    // 若 system bus policy 不允許註冊服務名稱，改用 user bus 仍可完成本機 Demo 與 busctl 驗證。
    int result = sd_bus_open_system(&bus_);
    if (result >= 0)
    {
        result = sd_bus_request_name(bus_, serviceName, 0);
        if (result >= 0)
        {
            busMode_ = "system";
            return;
        }

        spdlog::warn(
            "Failed to request D-Bus service name on system bus ({}), falling back to user bus",
            std::strerror(-result));
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
    else
    {
        spdlog::warn(
            "Failed to connect to system bus ({}), falling back to user bus", std::strerror(-result));
    }

    result = sd_bus_open_user(&bus_);
    if (result < 0)
    {
        spdlog::error("Failed to connect to user bus after system bus fallback: {}", std::strerror(-result));
        throw std::runtime_error("Failed to connect to system or user bus");
    }

    result = sd_bus_request_name(bus_, serviceName, 0);
    if (result < 0)
    {
        spdlog::error("Failed to request D-Bus service name on user bus: {}", std::strerror(-result));
        throw std::runtime_error("Failed to request D-Bus service name on user bus");
    }

    busMode_ = "user";
}

void DbusBridge::registerObjects()
{
    // 先註冊固定物件，再依平台設定檔建立 GPU / Fan 感測物件。
    registerObject(serverPath, serverInterface, serverVtable, ObjectKind::Server, 0);
    registerObject(powerPath, powerInterface, powerVtable, ObjectKind::Power, 0);
    registerObject(eventPath, eventInterface, eventVtable, ObjectKind::Events, 0);

    const auto platform = managementService_.getPlatformSnapshot();
    for (std::size_t index = 0; index < platform.hardware.gpus.size(); ++index)
    {
        const std::string path = std::string(sensorBasePath) + "/gpu" + std::to_string(index);
        registerObject(path, sensorInterface, gpuSensorVtable, ObjectKind::GpuSensor, index);
    }

    for (std::size_t index = 0; index < platform.hardware.fans.size(); ++index)
    {
        const std::string path = std::string(sensorBasePath) + "/fan" + std::to_string(index);
        registerObject(path, sensorInterface, fanSensorVtable, ObjectKind::FanSensor, index);
    }
}

void DbusBridge::processLoop()
{
    while (running_)
    {
        {
            std::lock_guard<std::mutex> lock(busMutex_);
            if (bus_ == nullptr)
            {
                return;
            }

            // sd_bus_process 一次呼叫可處理多個待處理訊息，直到佇列清空再進入等待。
            while (sd_bus_process(bus_, nullptr) > 0)
            {}
        }

        {
            std::lock_guard<std::mutex> lock(busMutex_);
            if (bus_ == nullptr)
            {
                return;
            }

            // 最多等待 100ms，讓 stop() 能在合理時間內結束背景執行緒。
            sd_bus_wait(bus_, 100000);
        }
    }
}

void DbusBridge::registerObject(
    const std::string& path, const char* interfaceName, const sd_bus_vtable* vtable, ObjectKind kind,
    std::size_t index)
{
    auto context = std::make_unique<ObjectContext>();
    context->bridge = this;
    context->kind = kind;
    context->index = index;
    context->path = path;

    sd_bus_slot* slot = nullptr;
    const int result = sd_bus_add_object_vtable(bus_, &slot, path.c_str(), interfaceName, vtable, context.get());
    if (result < 0)
    {
        throw std::runtime_error("Failed to register D-Bus object at " + path);
    }

    contexts_.push_back(std::move(context));
    slots_.push_back(slot);
}

int DbusBridge::serverPropertyGetter(
    sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply, void* userdata, sd_bus_error*)
{
    const auto* context = static_cast<ObjectContext*>(userdata);
    const auto platform = context->bridge->managementService_.getPlatformSnapshot();

    if (std::strcmp(property, "SystemPowerBudgetWatts") == 0)
    {
        return sd_bus_message_append(reply, "i", platform.hardware.systemPowerBudgetWatts);
    }

    if (std::strcmp(property, "PowerCapActive") == 0)
    {
        return sd_bus_message_append(reply, "b", platform.hardware.powerCapActive);
    }

    if (std::strcmp(property, "FirmwareState") == 0)
    {
        const std::string state = common::toString(platform.firmware.state);
        return sd_bus_message_append(reply, "s", state.c_str());
    }

    return sd_bus_message_append(reply, "s", context->bridge->busMode_.c_str());
}

int DbusBridge::powerPropertyGetter(
    sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply, void* userdata, sd_bus_error*)
{
    const auto* context = static_cast<ObjectContext*>(userdata);
    const auto platform = context->bridge->managementService_.getPlatformSnapshot();
    const auto& telemetry = platform.hardware.powerTelemetry;

    if (std::strcmp(property, "TotalPower") == 0)
    {
        return sd_bus_message_append(reply, "d", telemetry.totalSystemPowerWatts);
    }

    if (std::strcmp(property, "TotalGpuPower") == 0)
    {
        return sd_bus_message_append(reply, "d", telemetry.totalGpuPowerWatts);
    }

    if (std::strcmp(property, "TotalFanPower") == 0)
    {
        return sd_bus_message_append(reply, "d", telemetry.totalFanPowerWatts);
    }

    if (std::strcmp(property, "TotalPsuPower") == 0)
    {
        return sd_bus_message_append(reply, "d", telemetry.totalPsuPowerWatts);
    }

    if (std::strcmp(property, "TotalNvmePower") == 0)
    {
        return sd_bus_message_append(reply, "d", telemetry.totalNvmePowerWatts);
    }

    return sd_bus_message_append(reply, "b", telemetry.budgetExceeded);
}

int DbusBridge::eventPropertyGetter(
    sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply, void* userdata, sd_bus_error*)
{
    const auto* context = static_cast<ObjectContext*>(userdata);
    const auto entries = context->bridge->managementService_.getEventLogEntries();

    if (std::strcmp(property, "EntryCount") == 0)
    {
        const std::uint64_t count = static_cast<std::uint64_t>(entries.size());
        return sd_bus_message_append(reply, "t", count);
    }

    const std::string lastEventId = entries.empty() ? "NONE" : entries.back().eventId;
    return sd_bus_message_append(reply, "s", lastEventId.c_str());
}

int DbusBridge::sensorPropertyGetter(
    sd_bus*, const char*, const char*, const char* property, sd_bus_message* reply, void* userdata, sd_bus_error*)
{
    const auto* context = static_cast<ObjectContext*>(userdata);
    const auto platform = context->bridge->managementService_.getPlatformSnapshot();

    if (context->kind == ObjectKind::GpuSensor)
    {
        if (context->index >= platform.hardware.gpus.size())
        {
            return -ENOENT;
        }

        const auto& gpu = platform.hardware.gpus.at(context->index);
        if (std::strcmp(property, "Temperature") == 0)
        {
            return sd_bus_message_append(reply, "d", gpu.temperatureCelsius);
        }

        if (std::strcmp(property, "Power") == 0)
        {
            return sd_bus_message_append(reply, "d", gpu.powerWatts);
        }

        if (std::strcmp(property, "Health") == 0)
        {
            return sd_bus_message_append(reply, "s", gpu.health.c_str());
        }

        return sd_bus_message_append(reply, "b", gpu.throttled);
    }

    if (context->index >= platform.hardware.fans.size())
    {
        return -ENOENT;
    }

    const auto& fan = platform.hardware.fans.at(context->index);
    if (std::strcmp(property, "Rpm") == 0)
    {
        return sd_bus_message_append(reply, "i", fan.rpm);
    }

    if (std::strcmp(property, "Pwm") == 0)
    {
        return sd_bus_message_append(reply, "i", fan.pwmPercent);
    }

    const char* health = fan.failed ? "Critical" : "OK";
    return sd_bus_message_append(reply, "s", health);
}

} // 命名空間 openbmc::dbus
