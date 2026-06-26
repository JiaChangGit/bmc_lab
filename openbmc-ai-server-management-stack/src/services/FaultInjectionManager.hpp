#pragma once

#include <functional>
#include <string>

namespace openbmc::hardware
{
class HardwareModel;
}

namespace openbmc::services
{

class FaultInjectionManager
{
  public:
    /*
     * 負責故障注入 (fault injection)，並觸發後續感測輪詢。
     * 這裡不直接寫事件，避免繞過 Health / Thermal / Power 既有事件鏈 (event chain)。
     */
    FaultInjectionManager(hardware::HardwareModel& hardwareModel, std::function<void()> faultCallback);

    /*
     * 用途:
     *   注入 GPU 過溫，並在成功後要求感測服務立即輪詢。
     *
     * 輸入:
     *   gpuId - GPU ID 或數字索引字串，例如 "gpu0" 或 "0"。
     *
     * 輸出:
     *   找到目標且成功更新時回傳 true，目標不存在時回傳 false。
     *
     * 注意事項:
     *   事件由後續 manager 判斷產生，不在此層直接寫入。
     */
    bool injectGpuOverTemp(const std::string& gpuId);

    /*
     * 用途:
     *   注入風扇故障，成功後觸發一次感測輪詢。
     *
     * 輸入:
     *   fanId - Fan ID 或數字索引字串。
     *
     * 輸出:
     *   成功回傳 true，目標不存在回傳 false。
     */
    bool injectFanFailure(const std::string& fanId);

    /*
     * 用途:
     *   注入 PSU 故障，成功後觸發一次感測輪詢。
     *
     * 輸入:
     *   psuId - PSU ID 或數字索引字串。
     *
     * 輸出:
     *   成功回傳 true，目標不存在回傳 false。
     */
    bool injectPsuFailure(const std::string& psuId);

    /*
     * 用途:
     *   注入 NVMe 故障，成功後觸發一次感測輪詢。
     *
     * 輸入:
     *   nvmeId - NVMe ID 或數字索引字串。
     *
     * 輸出:
     *   成功回傳 true，目標不存在回傳 false。
     */
    bool injectNvmeFault(const std::string& nvmeId);

    /*
     * 用途:
     *   清除所有故障注入狀態並觸發一次感測更新。
     */
    void clearAllFaults();

  private:
    hardware::HardwareModel& hardwareModel_;
    std::function<void()> faultCallback_;
};

} // 命名空間 openbmc::services
