#pragma once

#include "common/ModelTypes.hpp"

#include <set>
#include <string>

namespace openbmc::services
{

class EventLogger;

class HealthMonitor
{
  public:
    /*
     * 偵測風扇、PSU、NVMe 這類故障事件 (fault-type events)。
     * 事件鎖存集合可避免同一個故障在每次輪詢時重複寫入。
     */
    explicit HealthMonitor(EventLogger& eventLogger);

    /*
     * 用途:
     *   檢查風扇、PSU、NVMe 的健康狀態，必要時寫入事件。
     *
     * 輸入:
     *   snapshot - 目前硬體狀態快照。
     *
     * 輸出:
     *   無回傳值；故障事件會寫入 EventLogger。
     *
     * 注意事項:
     *   使用 latch 集合避免同一故障在每次輪詢時重複記錄。
     */
    void evaluate(const common::HardwareSnapshot& snapshot);

  private:
    EventLogger& eventLogger_;
    std::set<std::string> fanFailureLatched_;
    std::set<std::string> psuFailureLatched_;
    std::set<std::string> nvmeFaultLatched_;
};

} // 命名空間 openbmc::services
