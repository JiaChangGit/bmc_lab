#include "services/HealthMonitor.hpp"

#include "services/EventLogger.hpp"

#include <sstream>

namespace openbmc::services
{

HealthMonitor::HealthMonitor(EventLogger& eventLogger) : eventLogger_(eventLogger) {}

void HealthMonitor::evaluate(const common::HardwareSnapshot& snapshot)
{
    for (const auto& fan : snapshot.fans)
    {
        if (fan.failed)
        {
            // 鎖存可避免 fan0 故障期間每秒都產生 FAN_FAILURE。
            if (fanFailureLatched_.insert(fan.id).second)
            {
                std::ostringstream message;
                message << fan.id << " reported RPM 0 and is marked failed";
                eventLogger_.logEvent("Critical", fan.id, message.str(), "FAN_FAILURE");
            }
            continue;
        }

        fanFailureLatched_.erase(fan.id);
    }

    for (const auto& psu : snapshot.psus)
    {
        if (!psu.healthy)
        {
            // PSU 故障會同時影響健康狀態與輸出功率，事件只記錄第一次轉壞。
            if (psuFailureLatched_.insert(psu.id).second)
            {
                std::ostringstream message;
                message << psu.id << " is unhealthy and no longer contributing output power";
                eventLogger_.logEvent("Critical", psu.id, message.str(), "PSU_FAILURE");
            }
            continue;
        }

        psuFailureLatched_.erase(psu.id);
    }

    for (const auto& nvme : snapshot.nvmes)
    {
        if (nvme.health != "OK")
        {
            // NVMe 使用 health 字串判斷，讓 Warning / Critical 都能走同一條事件路徑。
            if (nvmeFaultLatched_.insert(nvme.id).second)
            {
                std::ostringstream message;
                message << nvme.id << " health changed to " << nvme.health
                        << " at " << nvme.temperatureCelsius << "C";
                eventLogger_.logEvent("Warning", nvme.id, message.str(), "NVME_FAULT");
            }
            continue;
        }

        nvmeFaultLatched_.erase(nvme.id);
    }
}

} // 命名空間 openbmc::services
