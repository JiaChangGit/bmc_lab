#include "services/ManagementService.hpp"

#include "hardware/HardwareModel.hpp"
#include "services/EventLogger.hpp"
#include "services/FaultInjectionManager.hpp"
#include "services/FirmwareUpdateManager.hpp"

namespace openbmc::services
{

ManagementService::ManagementService(
    hardware::HardwareModel& hardwareModel, EventLogger& eventLogger, FaultInjectionManager& faultInjectionManager,
    FirmwareUpdateManager& firmwareUpdateManager) :
    hardwareModel_(hardwareModel),
    eventLogger_(eventLogger),
    faultInjectionManager_(faultInjectionManager),
    firmwareUpdateManager_(firmwareUpdateManager)
{}

common::PlatformSnapshot ManagementService::getPlatformSnapshot() const
{
    // 對外查詢只回傳 snapshot 副本，避免 HTTP/D-Bus 呼叫端直接持有硬體模型鎖。
    common::PlatformSnapshot snapshot;
    snapshot.hardware = hardwareModel_.snapshot();
    snapshot.firmware = firmwareUpdateManager_.status();
    return snapshot;
}

std::vector<common::EventRecord> ManagementService::getEventLogEntries() const
{
    return eventLogger_.entries();
}

bool ManagementService::injectGpuOverTemp(const std::string& gpuId)
{
    return faultInjectionManager_.injectGpuOverTemp(gpuId);
}

bool ManagementService::injectFanFailure(const std::string& fanId)
{
    return faultInjectionManager_.injectFanFailure(fanId);
}

bool ManagementService::injectPsuFailure(const std::string& psuId)
{
    return faultInjectionManager_.injectPsuFailure(psuId);
}

bool ManagementService::injectNvmeFault(const std::string& nvmeId)
{
    return faultInjectionManager_.injectNvmeFault(nvmeId);
}

void ManagementService::clearFaults()
{
    faultInjectionManager_.clearAllFaults();
}

bool ManagementService::startFirmwareUpdate(const std::string& imageUri, std::string& message)
{
    return firmwareUpdateManager_.startUpdate(imageUri, message);
}

} // 命名空間 openbmc::services
