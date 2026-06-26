#include "libs/pcie/pci_sysfs_reader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

namespace {
class TemporaryTree {
  public:
    TemporaryTree() {
        root = std::filesystem::temp_directory_path() /
               ("mini-pci-test-" + std::to_string(::getpid()));
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root / "0000:01:00.0");
    }
    ~TemporaryTree() { std::filesystem::remove_all(root); }
    void write(const std::string& name, const std::string& value) {
        std::ofstream(root / "0000:01:00.0" / name) << value << '\n';
    }
    std::filesystem::path root;
};
} // namespace

TEST(PciSysfsReader, ReadsPresentFieldsAndDriverLink) {
    TemporaryTree tree;
    tree.write("vendor", "0x10de");
    tree.write("device", "0x1234");
    tree.write("class", "0x030000");
    tree.write("current_link_width", "16");
    tree.write("current_link_speed", "16.0 GT/s");
    std::filesystem::create_directories(tree.root / "drivers" / "nvidia");
    std::filesystem::create_symlink(tree.root / "drivers" / "nvidia",
                                    tree.root / "0000:01:00.0" / "driver");
    pcie::PciSysfsReader reader(tree.root);
    auto devices = reader.scan();
    ASSERT_TRUE(devices.ok()) << devices.status().message();
    ASSERT_EQ(devices.value().size(), 1U);
    const auto& device = devices.value().front();
    EXPECT_EQ(device.vendor, "0x10de");
    EXPECT_EQ(device.currentLinkWidth, 16);
    EXPECT_EQ(device.driver, "nvidia");
}

TEST(PciSysfsReader, MissingOptionalFilesDoNotFail) {
    TemporaryTree tree;
    pcie::PciSysfsReader reader(tree.root);
    auto devices = reader.scan();
    ASSERT_TRUE(devices.ok());
    ASSERT_FALSE(devices.value().empty());
    const auto iterator = std::find_if(
        devices.value().begin(), devices.value().end(),
        [](const auto& item) { return item.bdf == "0000:01:00.0"; });
    ASSERT_NE(iterator, devices.value().end());
    EXPECT_FALSE(iterator->maxLinkSpeed);
    EXPECT_FALSE(iterator->numaNode);
}
