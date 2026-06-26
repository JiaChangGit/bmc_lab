#pragma once

#include "libs/pldm/pldm_common.hpp"

#include <cstdint>

namespace pldm {

enum class BaseCommand : std::uint8_t {
    setTid = 0x01,
    getTid = 0x02,
    getPldmVersion = 0x03,
    getPldmTypes = 0x04,
    getPldmCommands = 0x05,
};

class Type0Responder {
  public:
    explicit Type0Responder(std::uint8_t tid = 8);
    Message handle(const Message& request);
    [[nodiscard]] std::uint8_t tid() const { return tid_; }

  private:
    std::uint8_t tid_;
};

common::StatusOr<std::uint8_t> parseGetTidResponse(const Message& response);

} // namespace pldm
