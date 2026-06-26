#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace openbmc::common
{

/*
 * 韌體更新狀態 (Firmware Update State)。
 * 這個列舉值描述 FirmwareUpdateManager 目前走到哪個階段，Redfish 風格
 * UpdateService API 會把它轉成 JSON 回傳給使用者。
 */
enum class FirmwareUpdateState
{
    Idle,
    Downloading,
    Verifying,
    Installing,
    RebootPending,
    Completed,
    Rollback,
};

/*
 * 將 FirmwareUpdateState 轉成穩定字串。
 *
 * 用途:
 *   對外 API 與事件紀錄需要可讀的狀態字串，不應直接輸出 enum 數值。
 *
 * 注意事項:
 *   回傳值會出現在 JSON，因此改字串時需同步檢查文件與測試。
 */
inline std::string toString(FirmwareUpdateState state)
{
    switch (state)
    {
        case FirmwareUpdateState::Idle:
            return "Idle";
        case FirmwareUpdateState::Downloading:
            return "Downloading";
        case FirmwareUpdateState::Verifying:
            return "Verifying";
        case FirmwareUpdateState::Installing:
            return "Installing";
        case FirmwareUpdateState::RebootPending:
            return "RebootPending";
        case FirmwareUpdateState::Completed:
            return "Completed";
        case FirmwareUpdateState::Rollback:
            return "Rollback";
    }

    return "Unknown";
}

/*
 * GPU 裝置狀態 (Graphics Processing Unit Device State)。
 *
 * 欄位用途:
 *   id                    - API、D-Bus 與故障注入使用的元件 ID。
 *   temperatureCelsius    - 攝氏溫度，用於 ThermalManager 判斷風扇 PWM 與過溫事件。
 *   powerWatts            - GPU 功耗，用於 PowerManager 計算系統功耗。
 *   throttled             - 是否降頻；過溫或 power cap 啟用時會變成 true。
 *   health                - 健康狀態字串，例如 OK、Warning、Critical。
 *   faultInjectedOverTemp - 是否由故障注入 API 人為製造過溫狀態。
 */
struct GpuDevice
{
    std::string id;
    double temperatureCelsius {0.0};
    double powerWatts {0.0};
    bool throttled {false};
    std::string health {"OK"};
    bool faultInjectedOverTemp {false};
};

/*
 * 風扇裝置狀態 (Fan Device State)。
 *
 * PWM (Pulse Width Modulation，脈波寬度調變) 在本專案中用百分比表示，
 * ThermalManager 會依最高 GPU 溫度把所有風扇調到同一個 PWM。
 */
struct FanDevice
{
    std::string id;
    int rpm {0};
    int pwmPercent {0};
    bool failed {false};
    bool faultInjectedFailure {false};
};

/*
 * 電源供應器狀態 (Power Supply Unit, PSU)。
 *
 * outputWatts 是 PSU 端看到的輸出功率。本專案會依目前負載平均分配到健康的
 * PSU；故障 PSU 的輸出會設為 0。
 */
struct PsuDevice
{
    std::string id;
    double outputWatts {0.0};
    bool healthy {true};
    bool faultInjectedFailure {false};
};

/*
 * NVMe 裝置狀態 (Non-Volatile Memory Express Device State)。
 *
 * 本專案只模擬溫度與健康狀態，沒有實作真實磁碟讀寫、SMART 或 namespace 管理。
 */
struct NvmeDevice
{
    std::string id;
    double temperatureCelsius {0.0};
    std::string health {"OK"};
    bool faultInjectedFailure {false};
};

/*
 * CPU 裝置狀態 (Central Processing Unit Device State)。
 *
 * CPU 目前只出現在平台快照與 Redfish 風格 Systems 回應中；沒有 CPU 溫度、
 * 功耗控制或故障注入 API。
 */
struct CpuDevice
{
    std::string id;
    std::string model;
    bool healthy {true};
};

/*
 * 事件紀錄 (Event Record)。
 *
 * 事件由 ThermalManager、PowerManager、HealthMonitor 或 FirmwareUpdateManager
 * 產生，再由 EventLogger 保存，並可透過 HTTP EventLog API 查詢。
 */
struct EventRecord
{
    std::string timestamp;
    std::string severity;
    std::string component;
    std::string message;
    std::string eventId;
};

/*
 * 功耗遙測 (Power Telemetry)。
 *
 * totalSystemPowerWatts 是 PowerManager 用於判斷是否超過預算的合計值：
 * GPU + Fan + NVMe。totalPsuPowerWatts 是供電側觀察值，不能再加回系統功耗，
 * 否則會重複計算。
 */
struct PowerTelemetry
{
    double totalGpuPowerWatts {0.0};
    double totalFanPowerWatts {0.0};
    double totalPsuPowerWatts {0.0};
    double totalNvmePowerWatts {0.0};
    double totalSystemPowerWatts {0.0};
    bool budgetExceeded {false};
};

/*
 * 硬體快照 (Hardware Snapshot)。
 *
 * snapshot 是某個時間點的資料副本。HTTP API 與 D-Bus 都讀這份副本，
 * 避免外部介面直接操作 HardwareModel 內部狀態。
 */
struct HardwareSnapshot
{
    int systemPowerBudgetWatts {0};
    std::vector<GpuDevice> gpus;
    std::vector<FanDevice> fans;
    std::vector<PsuDevice> psus;
    std::vector<NvmeDevice> nvmes;
    std::vector<CpuDevice> cpus;
    PowerTelemetry powerTelemetry;
    bool powerCapActive {false};
};

/*
 * 韌體更新狀態快照 (Firmware Update Status)。
 *
 * busy 表示目前有更新流程在背景執行；Redfish 風格 SimpleUpdate API 會用這個欄位
 * 決定是否拒絕第二個更新請求。
 */
struct FirmwareUpdateStatus
{
    FirmwareUpdateState state {FirmwareUpdateState::Idle};
    std::string imageUri;
    std::string lastResult {"Idle"};
    bool busy {false};
};

/*
 * 平台快照 (Platform Snapshot)。
 *
 * ManagementService 會把硬體狀態與韌體更新狀態合併成這個結構，
 * 讓 HTTP 與 D-Bus 使用同一份資料來源。
 */
struct PlatformSnapshot
{
    HardwareSnapshot hardware;
    FirmwareUpdateStatus firmware;
};

} // 命名空間 openbmc::common
