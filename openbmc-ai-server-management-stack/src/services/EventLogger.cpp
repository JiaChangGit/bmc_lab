#include "services/EventLogger.hpp"

#include "common/TimeUtils.hpp"

#include <utility>

namespace openbmc::services
{

common::EventRecord EventLogger::logEvent(
    const std::string& severity, const std::string& component, const std::string& message,
    const std::string& eventId)
{
    common::EventRecord record;
    record.timestamp = common::makeUtcTimestamp();
    record.severity = severity;
    record.component = component;
    record.message = message;
    record.eventId = eventId;

    EventCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.size() >= maxEntries_)
        {
            // Ring buffer 類似行為：只保留最近 512 筆，避免 Demo 長時間執行後記憶體持續增加。
            entries_.erase(entries_.begin());
        }
        entries_.push_back(record);
        callback = callback_;
    }

    if (callback)
    {
        // callback 放在鎖外執行，避免 D-Bus signal 發送過程反向卡住 EventLogger。
        callback(record);
    }

    return record;
}

std::vector<common::EventRecord> EventLogger::entries() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

std::size_t EventLogger::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void EventLogger::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
}

void EventLogger::setEventCallback(EventCallback callback)
{
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
}

} // 命名空間 openbmc::services
