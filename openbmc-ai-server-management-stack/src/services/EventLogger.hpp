#pragma once

#include "common/ModelTypes.hpp"

#include <functional>
#include <mutex>
#include <vector>

namespace openbmc::services
{

class EventLogger
{
  public:
    /*
     * 集中保存事件 (events)，並在新增事件後通知觀察者 (observers)。
     * D-Bus bridge 透過 callback 把事件轉成 EventGenerated signal。
     */
    using EventCallback = std::function<void(const common::EventRecord&)>;

    /*
     * 用途:
     *   新增一筆事件並通知 callback。
     *
     * 輸入:
     *   severity  - 事件嚴重度，例如 Warning 或 Critical。
     *   component - 發生事件的元件 ID。
     *   message   - 事件文字。
     *   eventId   - 事件類型 ID，例如 FAN_FAILURE。
     *
     * 輸出:
     *   回傳實際寫入的 EventRecord。
     *
     * 注意事項:
     *   最多保留 512 筆，超過時會移除最舊事件；callback 在鎖外呼叫，避免鎖住觀察者。
     */
    common::EventRecord logEvent(
        const std::string& severity, const std::string& component, const std::string& message,
        const std::string& eventId);

    /*
     * 用途:
     *   取得目前事件列表副本。
     *
     * 輸出:
     *   回傳 EventRecord vector。
     */
    std::vector<common::EventRecord> entries() const;

    /*
     * 用途:
     *   取得目前保存的事件數量，主要供測試確認事件是否被寫入。
     */
    std::size_t size() const;

    /*
     * 用途:
     *   清空事件列表。
     *
     * 注意事項:
     *   目前不會另外發出清除事件，也不會通知 callback。
     */
    void clear();

    /*
     * 用途:
     *   設定事件新增後要通知的觀察者。
     *
     * 注意事項:
     *   DbusBridge 會用這個 callback 發出 EventGenerated signal。
     */
    void setEventCallback(EventCallback callback);

  private:
    mutable std::mutex mutex_;
    std::vector<common::EventRecord> entries_;
    EventCallback callback_;
    static constexpr std::size_t maxEntries_ = 512;
};

} // 命名空間 openbmc::services
