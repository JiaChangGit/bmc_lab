#include "services/EventLogger.hpp"

#include <gtest/gtest.h>

#include <string>
#include <thread>
#include <vector>

namespace openbmc::tests
{

/*
 * 驗證 EventLogger 的基本 thread-safety。
 *
 * 測試方式:
 *   建立 4 條 thread，每條各寫入 25 筆事件。
 *
 * 預期結果:
 *   最後事件數量為 100，代表多執行緒寫入時沒有遺失事件。
 */
TEST(EventLoggerTest, StoresEventsThreadSafelyAcrossMultipleThreads)
{
    services::EventLogger eventLogger;
    std::vector<std::thread> workers;

    for (int threadIndex = 0; threadIndex < 4; ++threadIndex)
    {
        workers.emplace_back([&eventLogger, threadIndex]() {
            for (int eventIndex = 0; eventIndex < 25; ++eventIndex)
            {
                eventLogger.logEvent(
                    "Info", "thread" + std::to_string(threadIndex), "message", "GPU_OVER_TEMP");
            }
        });
    }

    for (auto& worker : workers)
    {
        worker.join();
    }

    EXPECT_EQ(eventLogger.size(), 100U);
    EXPECT_EQ(eventLogger.entries().front().eventId, "GPU_OVER_TEMP");
}

} // 命名空間 openbmc::tests
