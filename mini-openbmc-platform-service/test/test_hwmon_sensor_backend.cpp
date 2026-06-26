#include "libs/hwmon/hwmon_sensor_backend.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

namespace {
class HwmonTree {
  public:
    HwmonTree() {
        parent = std::filesystem::temp_directory_path() /
                 ("mini-hwmon-" + std::to_string(::getpid()));
        root = parent / "hwmon7";
        std::filesystem::remove_all(parent);
        std::filesystem::create_directories(root);
        write("name", "mini_i2c_hwmon");
        write("temp1_input", "42000");
        write("temp1_label", "CPU Board Temp");
        write("in1_input", "12000");
        write("in1_label", "Board Voltage");
        write("fan1_input", "8000");
        write("fan1_label", "Fan0 Tach");
        write("fault_mode", "none");
    }
    ~HwmonTree() { std::filesystem::remove_all(parent); }
    void write(const std::string& name, const std::string& value) {
        std::ofstream(root / name) << value << '\n';
    }
    std::filesystem::path parent;
    std::filesystem::path root;
};
} // namespace

TEST(HwmonBackend, DiscoversAndReadsSensors) {
    HwmonTree tree;
    auto discovered = hwmon::HwmonSensorBackend::discover(tree.parent);
    ASSERT_TRUE(discovered.ok());
    hwmon::HwmonSensorBackend backend(discovered.value());
    auto readings = backend.read();
    ASSERT_TRUE(readings.ok()) << readings.status().message();
    EXPECT_EQ(readings.value().temperatureMillic, 42000);
    EXPECT_EQ(readings.value().voltageMillivolt, 12000);
    EXPECT_EQ(readings.value().fanRpm, 8000);
}

TEST(HwmonBackend, MissingSensorFileReturnsStatus) {
    HwmonTree tree;
    std::filesystem::remove(tree.root / "fan1_input");
    hwmon::HwmonSensorBackend backend(tree.root);
    auto readings = backend.read();
    EXPECT_FALSE(readings.ok());
    EXPECT_EQ(readings.status().code(), common::StatusCode::notFound);
}
