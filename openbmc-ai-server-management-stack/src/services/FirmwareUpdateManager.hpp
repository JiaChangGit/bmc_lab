#pragma once

#include "common/ModelTypes.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace openbmc::services
{

class EventLogger;

class FirmwareUpdateManager
{
  public:
    /*
     * 模擬韌體更新狀態機 (firmware update state machine)。
     * 流程刻意保留 Download / Verify / Install / Rollback，方便觀察成功與失敗路徑。
     */
    explicit FirmwareUpdateManager(EventLogger& eventLogger);
    ~FirmwareUpdateManager();

    /*
     * 用途:
     *   啟動非同步韌體更新流程。
     *
     * 輸入:
     *   imageUri - 韌體映像 URI；目前只用字串規則模擬成功或失敗。
     *   message  - 回傳啟動結果或拒絕原因。
     *
     * 輸出:
     *   true  表示已接受並啟動背景流程。
     *   false 表示拒絕，例如 imageUri 空字串或已有更新在執行。
     *
     * 注意事項:
     *   這裡不下載、不驗簽、不燒錄真實韌體。
     */
    bool startUpdate(const std::string& imageUri, std::string& message);

    /*
     * 用途:
     *   讀取目前韌體更新狀態。
     *
     * 輸出:
     *   回傳 FirmwareUpdateStatus 副本。
     */
    common::FirmwareUpdateStatus status() const;

    /*
     * 用途:
     *   設定狀態改變時要通知的 callback。
     *
     * 注意事項:
     *   Application 用它通知 D-Bus server 屬性更新。
     */
    void setStateChangeCallback(std::function<void()> callback);

  private:
    EventLogger& eventLogger_;
    mutable std::mutex mutex_;
    common::FirmwareUpdateStatus status_;
    std::function<void()> stateChangeCallback_;
    std::thread workerThread_;
    std::atomic<bool> stopRequested_ {false};

    void runUpdateWorkflow(std::string imageUri);
    void setStatus(common::FirmwareUpdateState state, bool busy, const std::string& result);
    void notifyStateChange();
};

} // 命名空間 openbmc::services
