#include "services/ThermalManager.hpp"

#include "hardware/HardwareModel.hpp"
#include "services/EventLogger.hpp"

#include <algorithm>
#include <sstream>

namespace openbmc::services
{

ThermalManager::ThermalManager(hardware::HardwareModel& hardwareModel, EventLogger& eventLogger) :
    hardwareModel_(hardwareModel),
    eventLogger_(eventLogger)
{}

void ThermalManager::evaluate(const common::HardwareSnapshot& snapshot)
{
    // 先找最高 GPU 溫度，再用同一個 PWM 套到所有風扇，模擬整機散熱策略。
    double maxGpuTemperature = 0.0;
    for (const auto& gpu : snapshot.gpus)
    {
        maxGpuTemperature = std::max(maxGpuTemperature, gpu.temperatureCelsius);
    }

    int fanPwm = 40;
    if (maxGpuTemperature >= 70.0 && maxGpuTemperature <= 85.0)
    {
        fanPwm = 70;
    }
    else if (maxGpuTemperature > 85.0)
    {
        fanPwm = 100;
    }

    hardwareModel_.setAllFanPwm(fanPwm);

    for (const auto& gpu : snapshot.gpus)
    {
        if (gpu.temperatureCelsius > 85.0)
        {
            // 同一張 GPU 持續過溫時只記錄一次，避免每輪輪詢都新增重複事件。
            if (overTempLatched_.insert(gpu.id).second)
            {
                std::ostringstream message;
                message << gpu.id << " temperature reached " << gpu.temperatureCelsius
                        << "C and fan policy escalated to " << fanPwm << "%";

                if (gpu.temperatureCelsius > 90.0)
                {
                    message << "; thermal throttling is active";
                }

                eventLogger_.logEvent(
                    gpu.temperatureCelsius > 90.0 ? "Critical" : "Warning", gpu.id, message.str(),
                    "GPU_OVER_TEMP");
            }
            continue;
        }

        // 溫度回到門檻以下後解除鎖存，下一次過溫才會重新記錄。
        overTempLatched_.erase(gpu.id);
    }
}

} // 命名空間 openbmc::services
