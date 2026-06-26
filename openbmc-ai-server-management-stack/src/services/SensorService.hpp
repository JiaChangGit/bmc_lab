#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace openbmc::hardware
{
class HardwareModel;
}

namespace openbmc::services
{

class HealthMonitor;
class ThermalManager;
class PowerManager;

class SensorService
{
  public:
    /*
     * 以背景執行緒週期性輪詢感測資料。
     * 用來模擬 BMC 守護行程 (daemon) 持續更新遙測 (telemetry) 與策略判斷。
     */
    SensorService(
        hardware::HardwareModel& hardwareModel, HealthMonitor& healthMonitor, ThermalManager& thermalManager,
        PowerManager& powerManager, std::function<void()> cycleCallback,
        std::chrono::milliseconds pollInterval = std::chrono::milliseconds(1000));
    ~SensorService();

    /*
     * 用途:
     *   啟動背景感測輪詢執行緒。
     *
     * 輸入/輸出:
     *   無。
     *
     * 錯誤處理:
     *   若已經啟動，重複呼叫不會再建立新執行緒。
     */
    void start();

    /*
     * 用途:
     *   停止背景輪詢並等待執行緒結束。
     *
     * 注意事項:
     *   會喚醒 condition variable，避免執行緒卡在等待下一輪。
     */
    void stop();

    /*
     * 用途:
     *   要求感測服務盡快執行一輪。
     *
     * 使用情境:
     *   故障注入後呼叫，讓 API 結果不必等滿一個輪詢週期。
     */
    void requestImmediateCycle();

    /*
     * 用途:
     *   執行單輪感測與策略流程。
     *
     * 流程:
     *   simulateSensorTick -> HealthMonitor -> ThermalManager -> snapshot -> PowerManager -> callback。
     *
     * 注意事項:
     *   單元測試可直接呼叫此函式，避免依賴背景執行緒時間。
     */
    void runSingleCycle();

  private:
    hardware::HardwareModel& hardwareModel_;
    HealthMonitor& healthMonitor_;
    ThermalManager& thermalManager_;
    PowerManager& powerManager_;
    std::function<void()> cycleCallback_;
    std::chrono::milliseconds pollInterval_;
    std::atomic<bool> running_ {false};
    std::thread workerThread_;
    std::mutex mutex_;
    std::condition_variable conditionVariable_;
    bool immediateRequested_ {false};

    void workerLoop();
};

} // 命名空間 openbmc::services
