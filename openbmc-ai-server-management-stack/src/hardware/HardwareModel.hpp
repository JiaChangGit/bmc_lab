#pragma once

#include "common/ModelTypes.hpp"
#include "hardware/AIServerProfile.hpp"

#include <mutex>
#include <random>
#include <string>

namespace openbmc::hardware
{

class HardwareModel
{
  public:
    /*
     * 用途:
     *   以 AIServerProfile 建立可變動的硬體模型。
     *
     * 輸入:
     *   profile - 從 JSON 設定檔解析出的初始硬體資料。
     *
     * 注意事項:
     *   baselineProfile_ 保留原始設定，clearFaults() 會用它還原故障注入前的狀態。
     */
    explicit HardwareModel(AIServerProfile profile);

    /*
     * 用途:
     *   取得目前平台狀態的唯讀快照。
     *
     * 輸入:
     *   無。
     *
     * 輸出:
     *   回傳 HardwareSnapshot，包含 GPU、Fan、PSU、NVMe、CPU 與功耗遙測。
     *
     * 錯誤處理:
     *   不丟出例外；讀取時會用 mutex 保護共享狀態。
     */
    common::HardwareSnapshot snapshot() const;

    /*
     * 用途:
     *   推進一次模擬感測資料，更新溫度、功耗、風扇轉速與 PSU 輸出。
     *
     * 輸入/輸出:
     *   無輸入，直接更新內部硬體狀態。
     *
     * 注意事項:
     *   這是模擬資料，不代表真實硬體讀值。
     */
    void simulateSensorTick();

    /*
     * 用途:
     *   將指定 GPU 設為過溫狀態。
     *
     * 輸入:
     *   gpuId - GPU ID 或數字索引字串，例如 "gpu0" 或 "0"。
     *
     * 輸出:
     *   找到並更新目標時回傳 true，否則回傳 false。
     *
     * 注意事項:
     *   事件不在這裡直接產生，而是由後續 SensorService 週期判斷。
     */
    bool injectGpuOverTemp(const std::string& gpuId);

    /*
     * 用途:
     *   將指定風扇設為故障狀態。
     *
     * 輸入:
     *   fanId - Fan ID 或數字索引字串，例如 "fan0" 或 "0"。
     *
     * 輸出:
     *   找到並更新目標時回傳 true，否則回傳 false。
     *
     * 注意事項:
     *   事件由 HealthMonitor 在後續輪詢時產生。
     */
    bool injectFanFailure(const std::string& fanId);

    /*
     * 用途:
     *   將指定 PSU 設為失效狀態。
     *
     * 輸入:
     *   psuId - PSU ID 或數字索引字串，例如 "psu0" 或 "0"。
     *
     * 輸出:
     *   找到並更新目標時回傳 true，否則回傳 false。
     */
    bool injectPsuFailure(const std::string& psuId);

    /*
     * 用途:
     *   將指定 NVMe 設為故障狀態。
     *
     * 輸入:
     *   nvmeId - NVMe ID 或數字索引字串，例如 "nvme0" 或 "0"。
     *
     * 輸出:
     *   找到並更新目標時回傳 true，否則回傳 false。
     */
    bool injectNvmeFault(const std::string& nvmeId);

    /*
     * 用途:
     *   清除故障注入造成的 GPU、Fan、PSU、NVMe 狀態。
     *
     * 輸入/輸出:
     *   無輸入，直接更新內部硬體狀態。
     *
     * 注意事項:
     *   功耗上限與遙測結果會在後續感測與功耗週期重新計算。
     */
    void clearFaults();

    /*
     * 用途:
     *   套用全機共用風扇 PWM。
     *
     * 輸入:
     *   pwmPercent - PWM 百分比。
     *
     * 錯誤處理:
     *   會把輸入值限制在 0 到 100，避免風扇 PWM 超出百分比範圍。
     */
    void setAllFanPwm(int pwmPercent);

    /*
     * 用途:
     *   更新 power cap 狀態。
     *
     * 輸入:
     *   active - 是否啟用 power cap。
     *
     * 注意事項:
     *   power cap 會影響 GPU throttled 欄位。
     */
    void setPowerCapActive(bool active);

    /*
     * 用途:
     *   保存 PowerManager 計算後的功耗遙測結果。
     *
     * 輸入:
     *   telemetry - 功耗彙總資料。
     *
     * 注意事項:
     *   這份資料會出現在 HTTP Power API 與 D-Bus Power 屬性。
     */
    void updatePowerTelemetry(const common::PowerTelemetry& telemetry);

  private:
    /*
     * 保存可變動硬體狀態 (mutable hardware state)。
     * 上層服務只能透過公開方法改變狀態，避免 HTTP 或 D-Bus 直接改資料。
     */
    mutable std::mutex mutex_;
    AIServerProfile baselineProfile_;
    std::vector<common::GpuDevice> gpus_;
    std::vector<common::FanDevice> fans_;
    std::vector<common::PsuDevice> psus_;
    std::vector<common::NvmeDevice> nvmes_;
    std::vector<common::CpuDevice> cpus_;
    common::PowerTelemetry powerTelemetry_;
    bool powerCapActive_ {false};
    std::mt19937 randomEngine_;

    void refreshGpuThrottleStateLocked();
    void refreshGpuHealthLocked();
    void refreshPsuOutputLocked();
};

} // 命名空間 openbmc::hardware
