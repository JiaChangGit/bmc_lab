#pragma once

#include "libs/pldm/pldm_common.hpp"
#include "libs/pldm/pldm_pdr.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pldm {

enum class PlatformCommand : std::uint8_t {
    setEventReceiver = 0x04,
    platformEventMessage = 0x0a,
    getSensorReading = 0x11,
    getPdrRepositoryInfo = 0x50,
    getPdr = 0x51,
    setFault = 0xf0,
};

struct NumericReading {
    std::uint16_t sensorId{};
    double value{};
    bool available{true};
};

class Type2Responder {
  public:
    Type2Responder(PdrRepository repository,
                   std::unordered_map<std::uint16_t, double> readings);
    Message handle(const Message& request);
    void setReading(std::uint16_t sensorId, double value);
    void setFault(std::string fault);

  private:
    PdrRepository repository_;
    std::unordered_map<std::uint16_t, double> readings_;
    std::string fault_;
};

common::StatusOr<NumericReading>
parseGetSensorReadingResponse(const Message& response);

} // namespace pldm
