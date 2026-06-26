#include "services/FirmwareUpdateManager.hpp"

#include "services/EventLogger.hpp"

#include <chrono>
#include <thread>
#include <utility>

namespace openbmc::services
{

FirmwareUpdateManager::FirmwareUpdateManager(EventLogger& eventLogger) : eventLogger_(eventLogger) {}

FirmwareUpdateManager::~FirmwareUpdateManager()
{
    stopRequested_ = true;
    if (workerThread_.joinable())
    {
        workerThread_.join();
    }
}

bool FirmwareUpdateManager::startUpdate(const std::string& imageUri, std::string& message)
{
    if (imageUri.empty())
    {
        message = "image_uri must not be empty";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (status_.busy)
        {
            // 同一時間只允許一個更新流程，第二個請求由 API 轉成 409 Conflict。
            message = "Firmware update already in progress";
            return false;
        }
    }

    if (workerThread_.joinable())
    {
        workerThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.imageUri = imageUri;
        status_.lastResult = "Update accepted";
    }

    workerThread_ = std::thread([this, imageUri]() { runUpdateWorkflow(imageUri); });
    message = "Firmware update workflow started";
    return true;
}

common::FirmwareUpdateStatus FirmwareUpdateManager::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void FirmwareUpdateManager::setStateChangeCallback(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    stateChangeCallback_ = std::move(callback);
}

void FirmwareUpdateManager::runUpdateWorkflow(std::string imageUri)
{
    stopRequested_ = false;

    // Demo 以固定延遲模擬 Download / Verify / Install 的非同步流程。
    eventLogger_.logEvent("Info", "UpdateService", "Firmware download started from " + imageUri, "FW_UPDATE_STARTED");
    setStatus(common::FirmwareUpdateState::Downloading, true, "Downloading firmware image");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (stopRequested_)
    {
        return;
    }

    eventLogger_.logEvent("Info", "UpdateService", "Firmware verification started for " + imageUri, "FW_VERIFY_STARTED");
    setStatus(common::FirmwareUpdateState::Verifying, true, "Verifying firmware image");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (imageUri.find("bad") != std::string::npos || imageUri.find("verify-fail") != std::string::npos ||
        imageUri.find("corrupt") != std::string::npos)
    {
        // 以檔名關鍵字穩定重現驗證失敗，方便不依賴真實韌體映像也能測 rollback。
        eventLogger_.logEvent("Critical", "UpdateService", "Firmware checksum verification failed", "FW_VERIFY_FAILED");
        eventLogger_.logEvent("Critical", "UpdateService", "Rollback triggered after verification failure", "FW_ROLLBACK_TRIGGERED");
        setStatus(common::FirmwareUpdateState::Rollback, false, "Verification failed and rollback triggered");
        return;
    }

    eventLogger_.logEvent("Info", "UpdateService", "Firmware install started", "FW_INSTALL_STARTED");
    setStatus(common::FirmwareUpdateState::Installing, true, "Installing firmware image");
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    if (imageUri.find("install-fail") != std::string::npos || imageUri.find("fail-install") != std::string::npos)
    {
        // 安裝失敗與驗證失敗都會進入 Rollback，但事件 ID 不同，方便判斷失敗階段。
        eventLogger_.logEvent("Critical", "UpdateService", "Firmware install failed", "FW_UPDATE_FAILED");
        eventLogger_.logEvent("Critical", "UpdateService", "Rollback triggered after install failure", "FW_ROLLBACK_TRIGGERED");
        setStatus(common::FirmwareUpdateState::Rollback, false, "Install failed and rollback triggered");
        return;
    }

    setStatus(common::FirmwareUpdateState::RebootPending, true, "Install complete, reboot pending");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    setStatus(common::FirmwareUpdateState::Completed, false, "Firmware update completed successfully");
    eventLogger_.logEvent("Info", "UpdateService", "Firmware update completed successfully", "FW_UPDATE_COMPLETED");
}

void FirmwareUpdateManager::setStatus(common::FirmwareUpdateState state, bool busy, const std::string& result)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_.state = state;
        status_.busy = busy;
        status_.lastResult = result;
    }

    notifyStateChange();
}

void FirmwareUpdateManager::notifyStateChange()
{
    std::function<void()> callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = stateChangeCallback_;
    }

    if (callback)
    {
        callback();
    }
}

} // 命名空間 openbmc::services
