#include "services/SensorService.hpp"

#include "hardware/HardwareModel.hpp"
#include "services/HealthMonitor.hpp"
#include "services/PowerManager.hpp"
#include "services/ThermalManager.hpp"

#include <utility>

namespace openbmc::services
{

SensorService::SensorService(
    hardware::HardwareModel& hardwareModel, HealthMonitor& healthMonitor, ThermalManager& thermalManager,
    PowerManager& powerManager, std::function<void()> cycleCallback, std::chrono::milliseconds pollInterval) :
    hardwareModel_(hardwareModel),
    healthMonitor_(healthMonitor),
    thermalManager_(thermalManager),
    powerManager_(powerManager),
    cycleCallback_(std::move(cycleCallback)),
    pollInterval_(pollInterval)
{}

SensorService::~SensorService()
{
    stop();
}

void SensorService::start()
{
    if (running_.exchange(true))
    {
        return;
    }

    workerThread_ = std::thread([this]() { workerLoop(); });
}

void SensorService::stop()
{
    if (!running_.exchange(false))
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        immediateRequested_ = true;
    }
    conditionVariable_.notify_all();

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }
}

void SensorService::requestImmediateCycle()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        immediateRequested_ = true;
    }
    conditionVariable_.notify_all();
}

void SensorService::runSingleCycle()
{
    // 一輪感測流程：先更新硬體模型，再讓健康、散熱、功耗策略依序判斷。
    hardwareModel_.simulateSensorTick();
    auto snapshot = hardwareModel_.snapshot();
    healthMonitor_.evaluate(snapshot);
    thermalManager_.evaluate(snapshot);
    // 散熱策略會依溫度調整風扇 PWM，因此功耗策略前重新取得一次快照。
    snapshot = hardwareModel_.snapshot();
    powerManager_.evaluate(snapshot);

    if (cycleCallback_)
    {
        cycleCallback_();
    }
}

void SensorService::workerLoop()
{
    // 啟動後先跑一輪，讓 API 一開始就有策略判斷後的資料。
    runSingleCycle();

    std::unique_lock<std::mutex> lock(mutex_);
    while (running_)
    {
        conditionVariable_.wait_for(lock, pollInterval_, [this]() {
            return !running_ || immediateRequested_;
        });

        if (!running_)
        {
            break;
        }

        immediateRequested_ = false;
        lock.unlock();
        // 執行策略時不持有 condition_variable 的 mutex，避免阻塞 requestImmediateCycle()。
        runSingleCycle();
        lock.lock();
    }
}

} // 命名空間 openbmc::services
