#pragma once

#include "common/ModelTypes.hpp"

#include <set>
#include <string>

namespace openbmc::hardware
{
class HardwareModel;
}

namespace openbmc::services
{

class EventLogger;

class ThermalManager
{
  public:
    /*
     * 實作 GPU 溫控策略 (thermal policy)。
     * 根據最高 GPU 溫度調整風扇 PWM，並在過溫時記錄事件。
     */
    ThermalManager(hardware::HardwareModel& hardwareModel, EventLogger& eventLogger);

    /*
     * 用途:
     *   根據 GPU 最高溫度決定全機風扇 PWM，並在過溫時記錄事件。
     *
     * 輸入:
     *   snapshot - 目前硬體狀態快照。
     *
     * 輸出:
     *   無回傳值；風扇 PWM 會寫回 HardwareModel。
     *
     * 注意事項:
     *   同一張 GPU 持續過溫時會被 overTempLatched_ 鎖存，避免每輪重複寫事件。
     */
    void evaluate(const common::HardwareSnapshot& snapshot);

  private:
    hardware::HardwareModel& hardwareModel_;
    EventLogger& eventLogger_;
    std::set<std::string> overTempLatched_;
};

} // 命名空間 openbmc::services
