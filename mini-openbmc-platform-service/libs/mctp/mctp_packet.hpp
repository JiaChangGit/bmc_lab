#pragma once

#include "libs/common/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mctp {

inline constexpr std::uint8_t kHeaderVersion = 1;
inline constexpr std::size_t kPayloadMtu = 64;
inline constexpr std::size_t kHeaderSize = 6;

enum class MessageType : std::uint8_t { control = 0x00, pldm = 0x01 };

struct Packet {
    std::uint8_t headerVersion{kHeaderVersion};
    std::uint8_t destinationEid{};
    std::uint8_t sourceEid{};
    bool som{};
    bool eom{};
    std::uint8_t packetSequence{};
    bool tagOwner{};
    std::uint8_t messageTag{};
    MessageType messageType{MessageType::pldm};
    std::vector<std::uint8_t> payload;
};

common::StatusOr<std::vector<std::uint8_t>> encodePacket(const Packet& packet);
common::StatusOr<Packet> decodePacket(std::span<const std::uint8_t> bytes);
common::StatusOr<std::vector<Packet>>
fragmentMessage(std::span<const std::uint8_t> payload, std::uint8_t destinationEid,
                std::uint8_t sourceEid, MessageType type, std::uint8_t messageTag,
                bool tagOwner);

} // namespace mctp
