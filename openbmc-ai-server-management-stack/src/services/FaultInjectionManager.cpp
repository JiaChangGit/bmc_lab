#include "services/FaultInjectionManager.hpp"

#include "hardware/HardwareModel.hpp"

#include <utility>

namespace openbmc::services
{

FaultInjectionManager::FaultInjectionManager(
    hardware::HardwareModel& hardwareModel, std::function<void()> faultCallback) :
    hardwareModel_(hardwareModel),
    faultCallback_(std::move(faultCallback))
{}

bool FaultInjectionManager::injectGpuOverTemp(const std::string& gpuId)
{
    const bool changed = hardwareModel_.injectGpuOverTemp(gpuId);
    if (changed && faultCallback_)
    {
        // 故障注入成功後立即要求感測服務跑一輪，API 查詢才會看到連動結果。
        faultCallback_();
    }
    return changed;
}

bool FaultInjectionManager::injectFanFailure(const std::string& fanId)
{
    const bool changed = hardwareModel_.injectFanFailure(fanId);
    if (changed && faultCallback_)
    {
        // 事件仍由 HealthMonitor 判斷，這裡只觸發後續輪詢。
        faultCallback_();
    }
    return changed;
}

bool FaultInjectionManager::injectPsuFailure(const std::string& psuId)
{
    const bool changed = hardwareModel_.injectPsuFailure(psuId);
    if (changed && faultCallback_)
    {
        faultCallback_();
    }
    return changed;
}

bool FaultInjectionManager::injectNvmeFault(const std::string& nvmeId)
{
    const bool changed = hardwareModel_.injectNvmeFault(nvmeId);
    if (changed && faultCallback_)
    {
        faultCallback_();
    }
    return changed;
}

void FaultInjectionManager::clearAllFaults()
{
    hardwareModel_.clearFaults();
    if (faultCallback_)
    {
        // 清除後也跑一輪，讓鎖存與 API 狀態能回到正常值。
        faultCallback_();
    }
}

} // 命名空間 openbmc::services
