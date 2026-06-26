#include "hardware/AIServerProfile.hpp"
#include "hardware/HardwareModel.hpp"
#include "services/EventLogger.hpp"
#include "services/PowerManager.hpp"
#include "tests/TestHelpers.hpp"

#include <gtest/gtest.h>

namespace openbmc::tests
{

/*
 * 驗證功耗超過預算時的 power cap 行為。
 *
 * Power Capping（功耗上限控制）在本專案中的效果:
 *   - powerCapActive 變成 true。
 *   - 所有 GPU 的 throttled 變成 true。
 *   - EventLog 寫入 POWER_CAP_TRIGGERED。
 */
TEST(PowerManagerTest, TriggersPowerCapAndThrottlesAllGpusWhenBudgetIsExceeded)
{
    auto json = makeProfileJson();
    json["system_power_budget_watts"] = 400;
    json["gpus"][0]["power_watts"] = 280.0;
    json["gpus"][1]["power_watts"] = 285.0;

    auto profile = hardware::AIServerProfile::fromJson(json);
    hardware::HardwareModel hardwareModel(profile);
    services::EventLogger eventLogger;
    services::PowerManager powerManager(hardwareModel, eventLogger);

    powerManager.evaluate(hardwareModel.snapshot());

    const auto snapshot = hardwareModel.snapshot();
    EXPECT_TRUE(snapshot.powerCapActive);
    EXPECT_TRUE(snapshot.powerTelemetry.budgetExceeded);
    EXPECT_GT(snapshot.powerTelemetry.totalSystemPowerWatts, 400.0);
    EXPECT_TRUE(snapshot.gpus.at(0).throttled);
    EXPECT_TRUE(snapshot.gpus.at(1).throttled);
    EXPECT_EQ(eventLogger.size(), 1U);
    EXPECT_EQ(eventLogger.entries().back().eventId, "POWER_CAP_TRIGGERED");
}

} // 命名空間 openbmc::tests
