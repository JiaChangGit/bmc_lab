#include "hardware/AIServerProfile.hpp"
#include "hardware/HardwareModel.hpp"
#include "services/EventLogger.hpp"
#include "services/FaultInjectionManager.hpp"
#include "services/HealthMonitor.hpp"
#include "services/PowerManager.hpp"
#include "services/SensorService.hpp"
#include "services/ThermalManager.hpp"
#include "tests/TestHelpers.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace openbmc::tests
{

/*
 * 驗證 Fan 故障注入會連動 SensorService 與 EventLog。
 *
 * 流程:
 *   injectFanFailure("0") -> SensorService::runSingleCycle()
 *   -> HealthMonitor 偵測 fan0 failed -> EventLogger 寫入 FAN_FAILURE。
 */
TEST(FaultInjectionManagerTest, FanFailureFlowsThroughSensorServiceIntoEventLog)
{
    auto profile = hardware::AIServerProfile::fromJson(makeProfileJson());
    hardware::HardwareModel hardwareModel(profile);
    services::EventLogger eventLogger;
    services::ThermalManager thermalManager(hardwareModel, eventLogger);
    services::PowerManager powerManager(hardwareModel, eventLogger);
    services::HealthMonitor healthMonitor(eventLogger);
    services::SensorService sensorService(
        hardwareModel, healthMonitor, thermalManager, powerManager, []() {}, std::chrono::milliseconds(50));
    services::FaultInjectionManager faultInjectionManager(hardwareModel, [&sensorService]() {
        sensorService.runSingleCycle();
    });

    // 故障注入接受數字索引字串；"0" 會對應到 fan0。
    EXPECT_TRUE(faultInjectionManager.injectFanFailure("0"));

    const auto snapshot = hardwareModel.snapshot();
    ASSERT_TRUE(snapshot.fans.at(0).failed);
    ASSERT_EQ(eventLogger.size(), 1U);
    EXPECT_EQ(eventLogger.entries().back().eventId, "FAN_FAILURE");
}

/*
 * 驗證 NVMe 故障注入與清除流程。
 *
 * 重點:
 *   - 注入後 EventLog 會出現 NVME_FAULT。
 *   - clearAllFaults() 會把 NVMe 狀態還原到 baseline profile。
 */
TEST(FaultInjectionManagerTest, NvmeFaultAndClearResetHardwareState)
{
    auto profile = hardware::AIServerProfile::fromJson(makeProfileJson());
    hardware::HardwareModel hardwareModel(profile);
    services::EventLogger eventLogger;
    services::ThermalManager thermalManager(hardwareModel, eventLogger);
    services::PowerManager powerManager(hardwareModel, eventLogger);
    services::HealthMonitor healthMonitor(eventLogger);
    services::SensorService sensorService(
        hardwareModel, healthMonitor, thermalManager, powerManager, []() {}, std::chrono::milliseconds(50));
    services::FaultInjectionManager faultInjectionManager(hardwareModel, [&sensorService]() {
        sensorService.runSingleCycle();
    });

    EXPECT_TRUE(faultInjectionManager.injectNvmeFault("0"));
    EXPECT_EQ(eventLogger.entries().back().eventId, "NVME_FAULT");

    faultInjectionManager.clearAllFaults();
    const auto snapshot = hardwareModel.snapshot();
    EXPECT_EQ(snapshot.nvmes.at(0).health, "OK");
    EXPECT_LT(snapshot.nvmes.at(0).temperatureCelsius, 50.0);
}

} // 命名空間 openbmc::tests
