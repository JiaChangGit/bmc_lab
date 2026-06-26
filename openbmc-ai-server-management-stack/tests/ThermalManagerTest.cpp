#include "hardware/AIServerProfile.hpp"
#include "hardware/HardwareModel.hpp"
#include "services/EventLogger.hpp"
#include "services/ThermalManager.hpp"
#include "tests/TestHelpers.hpp"

#include <gtest/gtest.h>

namespace openbmc::tests
{

/*
 * 驗證低溫區間的散熱策略。
 *
 * 規則:
 *   最高 GPU 溫度低於 70C 時，所有風扇維持 40% PWM。
 */
TEST(ThermalManagerTest, KeepsFanPwmAtFortyBelowSeventyCelsius)
{
    auto json = makeProfileJson();
    json["gpus"][0]["temperature_celsius"] = 65.0;
    json["gpus"][1]["temperature_celsius"] = 68.0;

    auto profile = hardware::AIServerProfile::fromJson(json);
    hardware::HardwareModel hardwareModel(profile);
    services::EventLogger eventLogger;
    services::ThermalManager thermalManager(hardwareModel, eventLogger);

    thermalManager.evaluate(hardwareModel.snapshot());

    const auto snapshot = hardwareModel.snapshot();
    EXPECT_EQ(snapshot.fans.at(0).pwmPercent, 40);
    EXPECT_EQ(snapshot.fans.at(1).pwmPercent, 40);
    EXPECT_EQ(eventLogger.size(), 0U);
}

/*
 * 驗證中溫區間的散熱策略。
 *
 * 規則:
 *   最高 GPU 溫度介於 70C 到 85C 時，所有風扇調整為 70% PWM。
 */
TEST(ThermalManagerTest, RaisesFanPwmToSeventyBetweenSeventyAndEightyFiveCelsius)
{
    auto json = makeProfileJson();
    json["gpus"][0]["temperature_celsius"] = 72.0;
    json["gpus"][1]["temperature_celsius"] = 74.0;

    auto profile = hardware::AIServerProfile::fromJson(json);
    hardware::HardwareModel hardwareModel(profile);
    services::EventLogger eventLogger;
    services::ThermalManager thermalManager(hardwareModel, eventLogger);

    thermalManager.evaluate(hardwareModel.snapshot());

    const auto snapshot = hardwareModel.snapshot();
    EXPECT_EQ(snapshot.fans.at(0).pwmPercent, 70);
    EXPECT_EQ(snapshot.fans.at(1).pwmPercent, 70);
}

/*
 * 驗證過溫區間的散熱與事件行為。
 *
 * 規則:
 *   GPU 溫度高於 85C 時，風扇調整為 100% PWM。
 *   GPU 溫度高於 90C 時，HardwareModel 會把該 GPU 標成 throttled。
 */
TEST(ThermalManagerTest, RaisesFanPwmToOneHundredAndLogsOverTempEventAboveEightyFiveCelsius)
{
    auto json = makeProfileJson();
    json["gpus"][0]["temperature_celsius"] = 95.0;
    json["gpus"][0]["health"] = "Critical";

    auto profile = hardware::AIServerProfile::fromJson(json);
    hardware::HardwareModel hardwareModel(profile);
    services::EventLogger eventLogger;
    services::ThermalManager thermalManager(hardwareModel, eventLogger);

    thermalManager.evaluate(hardwareModel.snapshot());

    const auto snapshot = hardwareModel.snapshot();
    EXPECT_EQ(snapshot.fans.at(0).pwmPercent, 100);
    EXPECT_TRUE(snapshot.gpus.at(0).throttled);
    EXPECT_EQ(eventLogger.size(), 1U);
    EXPECT_EQ(eventLogger.entries().back().eventId, "GPU_OVER_TEMP");
}

} // 命名空間 openbmc::tests
