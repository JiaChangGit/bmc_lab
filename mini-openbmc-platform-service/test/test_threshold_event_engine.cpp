#include "libs/sensor/threshold_event_engine.hpp"

#include <gtest/gtest.h>

namespace {
sensor::SensorReading reading(double value) {
    return {"GPU0_Core_Temp", "GPU0 Core Temperature",
            sensor::SensorType::temperature, value, "Cel",
            sensor::State::enabled, sensor::Health::ok, "test", "test", "test",
            "/test", "now", 85.0, std::nullopt};
}
} // namespace

TEST(ThresholdEventEngine, EmitsUpperCriticalOnceAndRecoversWithHysteresis) {
    sensor::ThresholdEventEngine engine;
    sensor::Threshold threshold{85.0, std::nullopt, 2.0,
                                sensor::Health::critical};
    auto asserted = engine.evaluate(reading(86.0), threshold);
    ASSERT_TRUE(asserted.event);
    EXPECT_EQ(asserted.health, sensor::Health::critical);
    EXPECT_FALSE(asserted.event->recovery);
    EXPECT_FALSE(engine.evaluate(reading(90.0), threshold).event);
    EXPECT_FALSE(engine.evaluate(reading(84.0), threshold).event);
    auto recovered = engine.evaluate(reading(83.0), threshold);
    ASSERT_TRUE(recovered.event);
    EXPECT_TRUE(recovered.event->recovery);
    EXPECT_EQ(recovered.health, sensor::Health::ok);
}

TEST(ThresholdEventEngine, HandlesLowerCriticalWithWarningSeverity) {
    sensor::ThresholdEventEngine engine;
    sensor::Threshold threshold{std::nullopt, 3000.0, 100.0,
                                sensor::Health::warning};
    auto fan = reading(2500.0);
    fan.id = "Fan0_Tach";
    fan.name = "Fan0 Tach";
    auto result = engine.evaluate(fan, threshold);
    ASSERT_TRUE(result.event);
    EXPECT_EQ(result.health, sensor::Health::warning);
    EXPECT_EQ(result.event->severity, "Warning");
}
