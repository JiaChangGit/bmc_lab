#pragma once

#include "libs/common/status.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace pcie {

enum class PcieFault {
    none,
    linkDown,
    linkDegraded,
    correctableErrorSpike,
    nonfatalError,
    telemetryTimeout,
    overTemperature,
    overPower,
};

struct PcieTelemetry {
    std::string deviceId;
    int linkWidth{};
    std::string linkSpeed;
    std::string linkState;
    std::int64_t temperatureMillic{};
    std::int64_t powerMilliwatt{};
    std::uint64_t correctableErrors{};
    std::uint64_t nonfatalErrors{};
    std::string health;
};

common::StatusOr<PcieFault> pcieFaultFromString(const std::string& value);
std::string toString(PcieFault fault);

class MiniPcieBackend {
  public:
    explicit MiniPcieBackend(std::filesystem::path root =
                                 "/sys/class/mini_bmc_pcie/mini_pcie0");
    common::StatusOr<PcieTelemetry> readTelemetry() const;
    common::Status setFault(PcieFault fault) const;

  private:
    std::filesystem::path root_;
};

} // namespace pcie
