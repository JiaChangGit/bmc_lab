#pragma once

#include "common/ModelTypes.hpp"

namespace openbmc::hardware
{
class HardwareModel;
}

namespace openbmc::services
{

class EventLogger;

class PowerManager
{
  public:
    /*
     * 實作平台功耗預算策略 (power budget policy)。
     * 計算整機功耗後更新硬體模型，讓 HTTP 與 D-Bus 看到同一份結果。
     */
    PowerManager(hardware::HardwareModel& hardwareModel, EventLogger& eventLogger);

    /*
     * 用途:
     *   根據 snapshot 計算總功耗並判斷是否超過功耗預算。
     *
     * 輸入:
     *   snapshot - SensorService 取得的硬體狀態快照。
     *
     * 輸出:
     *   無回傳值；結果會寫回 HardwareModel 的 telemetry 與 power cap 狀態。
     *
     * 錯誤處理:
     *   不丟出例外；事件寫入由 EventLogger 處理。
     *
     * 注意事項:
     *   TotalPower 目前只加總 GPU、Fan、NVMe；PSU 輸出不加回總功耗。
     */
    void evaluate(const common::HardwareSnapshot& snapshot);

  private:
    hardware::HardwareModel& hardwareModel_;
    EventLogger& eventLogger_;
    bool budgetExceededLatched_ {false};
};

} // 命名空間 openbmc::services
