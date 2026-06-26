#pragma once

#include "libs/mctp/mctp_packet.hpp"

#include <chrono>
#include <optional>
#include <vector>

namespace mctp {

class Reassembler {
  public:
    explicit Reassembler(std::chrono::milliseconds timeout =
                             std::chrono::milliseconds(1000));
    common::StatusOr<std::optional<std::vector<std::uint8_t>>>
        addPacket(const Packet& packet,
                  std::chrono::steady_clock::time_point now =
                      std::chrono::steady_clock::now());
    void reset();

  private:
    std::chrono::milliseconds timeout_;
    bool active_{};
    std::uint8_t expectedSequence_{};
    std::uint8_t messageTag_{};
    bool tagOwner_{};
    MessageType messageType_{MessageType::pldm};
    std::chrono::steady_clock::time_point lastPacket_{};
    std::vector<std::uint8_t> payload_;
};

} // namespace mctp
