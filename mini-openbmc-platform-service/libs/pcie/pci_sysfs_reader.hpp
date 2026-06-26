#pragma once

#include "libs/common/status.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pcie {

struct PciDeviceInfo {
    std::string bdf;
    std::optional<std::string> vendor;
    std::optional<std::string> device;
    std::optional<std::string> classCode;
    std::optional<std::string> revision;
    std::optional<std::string> driver;
    std::optional<std::string> currentLinkSpeed;
    std::optional<int> currentLinkWidth;
    std::optional<std::string> maxLinkSpeed;
    std::optional<int> maxLinkWidth;
    std::optional<int> numaNode;
};

class PciSysfsReader {
  public:
    explicit PciSysfsReader(
        std::filesystem::path root = "/sys/bus/pci/devices");
    common::StatusOr<std::vector<PciDeviceInfo>> scan() const;

  private:
    std::filesystem::path root_;
};

} // namespace pcie
