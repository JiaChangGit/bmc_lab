#include "services/PowerManager.hpp"

#include "hardware/HardwareModel.hpp"
#include "services/EventLogger.hpp"

#include <numeric>
#include <sstream>

namespace openbmc::services
{

PowerManager::PowerManager(hardware::HardwareModel& hardwareModel, EventLogger& eventLogger) :
    hardwareModel_(hardwareModel),
    eventLogger_(eventLogger)
{}

void PowerManager::evaluate(const common::HardwareSnapshot& snapshot)
{
    // 分類計算功耗，讓 Redfish Power API 可以同時呈現總功耗與元件拆分。
    const double totalGpuPower = std::accumulate(
        snapshot.gpus.begin(), snapshot.gpus.end(), 0.0,
        [](double sum, const common::GpuDevice& gpu) { return sum + gpu.powerWatts; });

    const double totalFanPower = std::accumulate(
        snapshot.fans.begin(), snapshot.fans.end(), 0.0,
        [](double sum, const common::FanDevice& fan) {
            return sum + (fan.failed ? 0.0 : 2.0 + static_cast<double>(fan.pwmPercent) * 0.12);
        });

    const double totalPsuPower = std::accumulate(
        snapshot.psus.begin(), snapshot.psus.end(), 0.0,
        [](double sum, const common::PsuDevice& psu) { return sum + psu.outputWatts; });

    const double totalNvmePower = std::accumulate(
        snapshot.nvmes.begin(), snapshot.nvmes.end(), 0.0,
        [](double sum, const common::NvmeDevice& nvme) {
            return sum + (nvme.health == "Critical" ? 16.0 : 12.0);
        });

    // PSU 輸出是供電側觀察值，不再加回 totalSystemPower，避免重複計算。
    const double totalSystemPower = totalGpuPower + totalFanPower + totalNvmePower;
    const bool budgetExceeded = totalSystemPower > static_cast<double>(snapshot.systemPowerBudgetWatts);

    common::PowerTelemetry telemetry;
    telemetry.totalGpuPowerWatts = totalGpuPower;
    telemetry.totalFanPowerWatts = totalFanPower;
    telemetry.totalPsuPowerWatts = totalPsuPower;
    telemetry.totalNvmePowerWatts = totalNvmePower;
    telemetry.totalSystemPowerWatts = totalSystemPower;
    telemetry.budgetExceeded = budgetExceeded;

    hardwareModel_.updatePowerTelemetry(telemetry);
    hardwareModel_.setPowerCapActive(budgetExceeded);

    if (budgetExceeded && !budgetExceededLatched_)
    {
        // 功耗超標只在狀態切換時記事件，持續超標時不重複刷 log。
        std::ostringstream message;
        message << "Platform power budget exceeded: " << totalSystemPower << "W > "
                << snapshot.systemPowerBudgetWatts << "W; all GPUs throttled";

        eventLogger_.logEvent("Critical", "PowerManager", message.str(), "POWER_CAP_TRIGGERED");
    }

    budgetExceededLatched_ = budgetExceeded;
}

} // 命名空間 openbmc::services
