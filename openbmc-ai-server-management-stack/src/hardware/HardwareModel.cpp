#include "hardware/HardwareModel.hpp"

#include <algorithm>
#include <cctype>
#include <numeric>
#include <utility>

namespace
{

/*
 * 將數值限制在指定範圍內 (clamp)。
 *
 * 用途:
 *   模擬感測器資料時避免溫度、功耗或 PWM 跑出專案預期範圍。
 */
template <typename T>
T clampValue(T value, T low, T high)
{
    return std::max(low, std::min(value, high));
}

/*
 * 判斷字串是否為純數字索引。
 *
 * 用途:
 *   故障注入 API 同時接受 "gpu0" 與 "0"。若使用者傳 "0"，會再由
 *   matchesRequestedId() 補上元件前綴。
 */
bool isSimpleIndex(const std::string& value)
{
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) { return std::isdigit(character) != 0; });
}

/*
 * 比對 API 傳入的元件 ID。
 *
 * 支援格式:
 *   - 含前綴 ID，例如 gpu0、fan0、psu0、nvme0。
 *   - 數字索引，例如 0，會依 prefix 轉成 gpu0 / fan0 / psu0 / nvme0。
 */
bool matchesRequestedId(const std::string& actualId, const std::string& requestedId, const char* prefix)
{
    if (actualId == requestedId)
    {
        return true;
    }

    if (isSimpleIndex(requestedId))
    {
        return actualId == std::string(prefix) + requestedId;
    }

    return false;
}

} // 匿名命名空間

namespace openbmc::hardware
{

HardwareModel::HardwareModel(AIServerProfile profile) :
    baselineProfile_(std::move(profile)),
    gpus_(baselineProfile_.gpus),
    fans_(baselineProfile_.fans),
    psus_(baselineProfile_.psus),
    nvmes_(baselineProfile_.nvmes),
    cpus_(baselineProfile_.cpus),
    randomEngine_(std::random_device {}())
{
    refreshGpuHealthLocked();
    refreshGpuThrottleStateLocked();
    refreshPsuOutputLocked();
}

common::HardwareSnapshot HardwareModel::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    common::HardwareSnapshot snapshot;
    snapshot.systemPowerBudgetWatts = baselineProfile_.systemPowerBudgetWatts;
    snapshot.gpus = gpus_;
    snapshot.fans = fans_;
    snapshot.psus = psus_;
    snapshot.nvmes = nvmes_;
    snapshot.cpus = cpus_;
    snapshot.powerTelemetry = powerTelemetry_;
    snapshot.powerCapActive = powerCapActive_;
    return snapshot;
}

void HardwareModel::simulateSensorTick()
{
    std::lock_guard<std::mutex> lock(mutex_);

    // 用小幅隨機值模擬感測器變動，讓 Demo 能看到狀態持續更新。
    std::uniform_real_distribution<double> gpuTempNoise(-1.5, 2.5);
    std::uniform_real_distribution<double> gpuPowerNoise(-12.0, 12.0);
    std::uniform_int_distribution<int> fanNoise(-120, 120);
    std::uniform_real_distribution<double> nvmeTempNoise(-0.8, 1.4);

    double averagePwm = 0.0;
    if (!fans_.empty())
    {
        averagePwm = std::accumulate(
            fans_.begin(), fans_.end(), 0.0,
            [](double sum, const common::FanDevice& fan) {
                return sum + static_cast<double>(fan.pwmPercent);
            }) /
            static_cast<double>(fans_.size());
    }

    const double coolingBias = averagePwm * 0.025;

    // GPU 溫度會受到故障注入、降頻狀態與風扇 PWM 影響。
    for (auto& gpu : gpus_)
    {
        if (gpu.faultInjectedOverTemp)
        {
            gpu.temperatureCelsius = clampValue(gpu.temperatureCelsius + gpuTempNoise(randomEngine_), 93.0, 98.0);
            gpu.powerWatts = clampValue(gpu.powerWatts + gpuPowerNoise(randomEngine_), 320.0, 350.0);
            gpu.health = "Critical";
        }
        else
        {
            const double workloadBias = gpu.throttled ? -12.0 : 5.0;
            gpu.temperatureCelsius =
                clampValue(gpu.temperatureCelsius + gpuTempNoise(randomEngine_) + workloadBias - coolingBias, 46.0, 88.0);

            const double basePower = gpu.throttled ? 180.0 : 245.0;
            gpu.powerWatts = clampValue(basePower + (gpu.temperatureCelsius - 55.0) * 2.8 + gpuPowerNoise(randomEngine_), 140.0, 310.0);
        }
    }

    refreshGpuHealthLocked();
    refreshGpuThrottleStateLocked();

    // 風扇故障時 RPM 固定為 0；正常風扇依 PWM 推估轉速。
    for (auto& fan : fans_)
    {
        if (fan.faultInjectedFailure || fan.failed)
        {
            fan.failed = true;
            fan.rpm = 0;
            continue;
        }

        fan.failed = false;
        const int calculatedRpm = 1200 + fan.pwmPercent * 55 + fanNoise(randomEngine_);
        fan.rpm = std::max(900, calculatedRpm);
    }

    // NVMe 故障用較高溫度與 Critical health 表示，方便 API 與事件路徑觀察。
    for (auto& nvme : nvmes_)
    {
        if (nvme.faultInjectedFailure)
        {
            nvme.temperatureCelsius = clampValue(nvme.temperatureCelsius + nvmeTempNoise(randomEngine_), 84.0, 92.0);
            nvme.health = "Critical";
            continue;
        }

        nvme.temperatureCelsius = clampValue(nvme.temperatureCelsius + nvmeTempNoise(randomEngine_), 32.0, 58.0);
        nvme.health = nvme.temperatureCelsius >= 65.0 ? "Warning" : "OK";
    }

    refreshPsuOutputLocked();
}

bool HardwareModel::injectGpuOverTemp(const std::string& gpuId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = std::find_if(gpus_.begin(), gpus_.end(), [&](const common::GpuDevice& gpu) {
        return matchesRequestedId(gpu.id, gpuId, "gpu");
    });

    if (iterator == gpus_.end())
    {
        return false;
    }

    iterator->faultInjectedOverTemp = true;
    iterator->temperatureCelsius = 95.0;
    iterator->powerWatts = 340.0;
    iterator->health = "Critical";
    refreshGpuThrottleStateLocked();
    refreshPsuOutputLocked();
    return true;
}

bool HardwareModel::injectFanFailure(const std::string& fanId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = std::find_if(fans_.begin(), fans_.end(), [&](const common::FanDevice& fan) {
        return matchesRequestedId(fan.id, fanId, "fan");
    });

    if (iterator == fans_.end())
    {
        return false;
    }

    iterator->faultInjectedFailure = true;
    iterator->failed = true;
    iterator->rpm = 0;
    return true;
}

bool HardwareModel::injectPsuFailure(const std::string& psuId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = std::find_if(psus_.begin(), psus_.end(), [&](const common::PsuDevice& psu) {
        return matchesRequestedId(psu.id, psuId, "psu");
    });

    if (iterator == psus_.end())
    {
        return false;
    }

    iterator->faultInjectedFailure = true;
    iterator->healthy = false;
    iterator->outputWatts = 0.0;
    refreshPsuOutputLocked();
    return true;
}

bool HardwareModel::injectNvmeFault(const std::string& nvmeId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto iterator = std::find_if(nvmes_.begin(), nvmes_.end(), [&](const common::NvmeDevice& nvme) {
        return matchesRequestedId(nvme.id, nvmeId, "nvme");
    });

    if (iterator == nvmes_.end())
    {
        return false;
    }

    iterator->faultInjectedFailure = true;
    iterator->temperatureCelsius = 88.0;
    iterator->health = "Critical";
    refreshPsuOutputLocked();
    return true;
}

void HardwareModel::clearFaults()
{
    std::lock_guard<std::mutex> lock(mutex_);
    gpus_ = baselineProfile_.gpus;
    fans_ = baselineProfile_.fans;
    psus_ = baselineProfile_.psus;
    nvmes_ = baselineProfile_.nvmes;
    refreshGpuHealthLocked();
    refreshGpuThrottleStateLocked();
    refreshPsuOutputLocked();
}

void HardwareModel::setAllFanPwm(int pwmPercent)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const int clampedPwm = clampValue(pwmPercent, 0, 100);
    for (auto& fan : fans_)
    {
        fan.pwmPercent = clampedPwm;
    }
}

void HardwareModel::setPowerCapActive(bool active)
{
    std::lock_guard<std::mutex> lock(mutex_);
    powerCapActive_ = active;
    refreshGpuThrottleStateLocked();
}

void HardwareModel::updatePowerTelemetry(const common::PowerTelemetry& telemetry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    powerTelemetry_ = telemetry;
}

void HardwareModel::refreshGpuThrottleStateLocked()
{
    // Power Capping（功耗上限控制）或 GPU 溫度超過 90C 時，GPU 進入 throttled 狀態。
    for (auto& gpu : gpus_)
    {
        gpu.throttled = powerCapActive_ || gpu.temperatureCelsius > 90.0;
    }
}

void HardwareModel::refreshGpuHealthLocked()
{
    // Health 狀態只依目前溫度重算；故障注入會先提高溫度，再走同一條判斷規則。
    for (auto& gpu : gpus_)
    {
        if (gpu.temperatureCelsius > 90.0)
        {
            gpu.health = "Critical";
        }
        else if (gpu.temperatureCelsius >= 85.0)
        {
            gpu.health = "Warning";
        }
        else
        {
            gpu.health = "OK";
        }
    }
}

void HardwareModel::refreshPsuOutputLocked()
{
    // PSU 輸出功率從目前硬體負載推估，供 Redfish Power API 顯示供電側觀察值。
    const double totalGpuPower = std::accumulate(
        gpus_.begin(), gpus_.end(), 0.0,
        [](double sum, const common::GpuDevice& gpu) { return sum + gpu.powerWatts; });

    const double totalFanPower = std::accumulate(
        fans_.begin(), fans_.end(), 0.0,
        [](double sum, const common::FanDevice& fan) {
            return sum + (fan.failed ? 0.0 : 2.0 + static_cast<double>(fan.pwmPercent) * 0.12);
        });

    const double totalNvmePower = std::accumulate(
        nvmes_.begin(), nvmes_.end(), 0.0,
        [](double sum, const common::NvmeDevice& nvme) {
            return sum + (nvme.health == "Critical" ? 16.0 : 12.0);
        });

    const double totalCpuPower = static_cast<double>(cpus_.size()) * 90.0;
    const double aggregateLoad = totalGpuPower + totalFanPower + totalNvmePower + totalCpuPower;

    // 只把負載平均分配給 healthy PSU；故障 PSU 保持 0W。
    std::size_t healthyPsuCount = 0;
    for (const auto& psu : psus_)
    {
        if (psu.healthy)
        {
            ++healthyPsuCount;
        }
    }

    for (auto& psu : psus_)
    {
        if (!psu.healthy || healthyPsuCount == 0)
        {
            psu.outputWatts = 0.0;
            continue;
        }

        psu.outputWatts = aggregateLoad / static_cast<double>(healthyPsuCount);
    }
}

} // 命名空間 openbmc::hardware
