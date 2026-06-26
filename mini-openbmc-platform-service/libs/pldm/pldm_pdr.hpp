#pragma once

#include "libs/common/status.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace pldm {

struct NumericSensorPdr {
    std::uint32_t recordHandle{};
    std::uint16_t sensorId{};
    std::string name;
    std::string unit;
    double upperCritical{};
    double lowerCritical{};
};

common::StatusOr<std::vector<std::uint8_t>> encodePdr(const NumericSensorPdr& pdr);
common::StatusOr<NumericSensorPdr> decodePdr(std::span<const std::uint8_t> bytes);

class PdrRepository {
  public:
    void add(NumericSensorPdr pdr);
    [[nodiscard]] const std::vector<NumericSensorPdr>& records() const;
    const NumericSensorPdr* find(std::uint32_t handle) const;
    [[nodiscard]] std::uint32_t nextHandle(std::uint32_t handle) const;

  private:
    std::vector<NumericSensorPdr> records_;
};

} // namespace pldm
