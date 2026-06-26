#pragma once

#include "common/ModelTypes.hpp"

#include <string>
#include <vector>

namespace openbmc::hardware
{
class HardwareModel;
}

namespace openbmc::services
{

class EventLogger;
class FaultInjectionManager;
class FirmwareUpdateManager;

class ManagementService
{
  public:
    /*
     * 提供 HTTP 與 D-Bus 共用的服務介面 (service interface)。
     * 這一層讓對外介面不用直接依賴硬體模型，資料來源也比較一致。
     */
    ManagementService(
        hardware::HardwareModel& hardwareModel, EventLogger& eventLogger, FaultInjectionManager& faultInjectionManager,
        FirmwareUpdateManager& firmwareUpdateManager);

    /*
     * 用途:
     *   取得平台狀態快照，供 HTTP 與 D-Bus 共用。
     *
     * 輸出:
     *   PlatformSnapshot，包含硬體 snapshot 與韌體更新狀態。
     */
    common::PlatformSnapshot getPlatformSnapshot() const;

    /*
     * 用途:
     *   取得事件記錄副本。
     *
     * 輸出:
     *   EventRecord vector。
     */
    std::vector<common::EventRecord> getEventLogEntries() const;

    /*
     * 用途:
     *   轉送 GPU 過溫注入請求到 FaultInjectionManager。
     *
     * 輸入:
     *   gpuId - GPU ID 或數字索引字串。
     *
     * 輸出:
     *   找到目標並注入成功時回傳 true，否則回傳 false。
     */
    bool injectGpuOverTemp(const std::string& gpuId);

    /*
     * 用途:
     *   轉送風扇故障注入請求到 FaultInjectionManager。
     *
     * 輸入:
     *   fanId - Fan ID 或數字索引字串。
     *
     * 輸出:
     *   找到目標並注入成功時回傳 true，否則回傳 false。
     */
    bool injectFanFailure(const std::string& fanId);

    /*
     * 用途:
     *   轉送 PSU 故障注入請求到 FaultInjectionManager。
     *
     * 輸入:
     *   psuId - PSU ID 或數字索引字串。
     *
     * 輸出:
     *   找到目標並注入成功時回傳 true，否則回傳 false。
     */
    bool injectPsuFailure(const std::string& psuId);

    /*
     * 用途:
     *   轉送 NVMe 故障注入請求到 FaultInjectionManager。
     *
     * 輸入:
     *   nvmeId - NVMe ID 或數字索引字串。
     *
     * 輸出:
     *   找到目標並注入成功時回傳 true，否則回傳 false。
     */
    bool injectNvmeFault(const std::string& nvmeId);

    /*
     * 用途:
     *   清除所有故障注入狀態。
     */
    void clearFaults();

    /*
     * 用途:
     *   啟動韌體更新流程。
     *
     * 輸入:
     *   imageUri - 韌體映像 URI。
     *   message  - 回傳啟動結果或拒絕原因。
     *
     * 輸出:
     *   true 表示接受，false 表示拒絕。
     */
    bool startFirmwareUpdate(const std::string& imageUri, std::string& message);

  private:
    hardware::HardwareModel& hardwareModel_;
    EventLogger& eventLogger_;
    FaultInjectionManager& faultInjectionManager_;
    FirmwareUpdateManager& firmwareUpdateManager_;
};

} // 命名空間 openbmc::services
