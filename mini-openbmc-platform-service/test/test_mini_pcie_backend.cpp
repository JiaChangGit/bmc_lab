#include "libs/pcie/mini_pcie_backend.hpp"
#include "libs/pcie/mini_pcie_ioctl.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

namespace {
class PcieTree {
  public:
    PcieTree() {
        root = std::filesystem::temp_directory_path() /
               ("mini-pcie-backend-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        write("device_id", "MiniGPU-0000");
        write("link_width", "16");
        write("link_speed", "16.0GT/s");
        write("link_state", "L0");
        write("gpu_core_temp_millic", "65000");
        write("gpu_power_milliwatt", "250000");
        write("correctable_error_count", "3");
        write("nonfatal_error_count", "1");
        write("health", "OK");
        write("fault_mode", "none");
    }
    ~PcieTree() { std::filesystem::remove_all(root); }
    void write(const std::string& name, const std::string& value) {
        std::ofstream(root / name) << value << '\n';
    }
    std::filesystem::path root;
};
} // namespace

TEST(MiniPcieBackend, ParsesTelemetry) {
    PcieTree tree;
    pcie::MiniPcieBackend backend(tree.root);
    auto telemetry = backend.readTelemetry();
    ASSERT_TRUE(telemetry.ok()) << telemetry.status().message();
    EXPECT_EQ(telemetry.value().linkWidth, 16);
    EXPECT_EQ(telemetry.value().temperatureMillic, 65000);
    EXPECT_EQ(telemetry.value().correctableErrors, 3U);
}

TEST(MiniPcieBackend, MapsFaultStrings) {
    auto fault = pcie::pcieFaultFromString("link_degraded");
    ASSERT_TRUE(fault.ok());
    EXPECT_EQ(fault.value(), pcie::PcieFault::linkDegraded);
    EXPECT_FALSE(pcie::pcieFaultFromString("bad").ok());
}

TEST(MiniPcieBackend, IoctlStructureAndTextAreStable) {
    mini_pcie_telemetry telemetry{};
    std::snprintf(telemetry.device_id, sizeof(telemetry.device_id), "MiniGPU");
    std::snprintf(telemetry.link_speed, sizeof(telemetry.link_speed), "16.0GT/s");
    std::snprintf(telemetry.link_state, sizeof(telemetry.link_state), "L0");
    std::snprintf(telemetry.health, sizeof(telemetry.health), "OK");
    telemetry.link_width = 16;
    const auto line = pcie::telemetryToLine(telemetry);
    EXPECT_NE(line.find("link_width=16"), std::string::npos);
    EXPECT_EQ(_IOC_TYPE(MINI_PCIE_GET_TELEMETRY), MINI_PCIE_IOC_MAGIC);
}
