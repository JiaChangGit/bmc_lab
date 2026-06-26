#include "libs/mctp/mctp_packet.hpp"

#include <algorithm>

namespace mctp {
namespace {

constexpr std::uint8_t kSomMask = 0x80;
constexpr std::uint8_t kEomMask = 0x40;
constexpr std::uint8_t kSequenceMask = 0x30;
constexpr std::uint8_t kTagOwnerMask = 0x08;
constexpr std::uint8_t kTagMask = 0x07;

bool validMessageType(std::uint8_t value) {
    return value == static_cast<std::uint8_t>(MessageType::control) ||
           value == static_cast<std::uint8_t>(MessageType::pldm);
}

} // namespace

common::StatusOr<std::vector<std::uint8_t>> encodePacket(const Packet& packet) {
    if (packet.headerVersion != kHeaderVersion) {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "unsupported MCTP header version");
    }
    if (packet.packetSequence > 3 || packet.messageTag > 7 ||
        packet.payload.size() > kPayloadMtu) {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "MCTP packet field is out of range");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderSize + packet.payload.size());
    bytes.push_back(packet.headerVersion);
    bytes.push_back(packet.destinationEid);
    bytes.push_back(packet.sourceEid);
    bytes.push_back(static_cast<std::uint8_t>(
        (packet.som ? kSomMask : 0) | (packet.eom ? kEomMask : 0) |
        ((packet.packetSequence & 0x03) << 4) |
        (packet.tagOwner ? kTagOwnerMask : 0) | (packet.messageTag & kTagMask)));
    bytes.push_back(static_cast<std::uint8_t>(packet.messageType));
    bytes.push_back(static_cast<std::uint8_t>(packet.payload.size()));
    bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
    return bytes;
}

common::StatusOr<Packet> decodePacket(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "MCTP packet is shorter than its header");
    }
    if (bytes[0] != kHeaderVersion) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "unsupported MCTP header version");
    }
    if (!validMessageType(bytes[4])) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "unknown MCTP message type");
    }
    const auto length = static_cast<std::size_t>(bytes[5]);
    if (length > kPayloadMtu || bytes.size() != kHeaderSize + length) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "MCTP payload length mismatch");
    }
    const auto flags = bytes[3];
    Packet packet;
    packet.headerVersion = bytes[0];
    packet.destinationEid = bytes[1];
    packet.sourceEid = bytes[2];
    packet.som = (flags & kSomMask) != 0;
    packet.eom = (flags & kEomMask) != 0;
    packet.packetSequence = static_cast<std::uint8_t>((flags & kSequenceMask) >> 4);
    packet.tagOwner = (flags & kTagOwnerMask) != 0;
    packet.messageTag = static_cast<std::uint8_t>(flags & kTagMask);
    packet.messageType = static_cast<MessageType>(bytes[4]);
    packet.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize),
                          bytes.end());
    return packet;
}

common::StatusOr<std::vector<Packet>>
fragmentMessage(std::span<const std::uint8_t> payload, std::uint8_t destinationEid,
                std::uint8_t sourceEid, MessageType type, std::uint8_t messageTag,
                bool tagOwner) {
    if (messageTag > 7) {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "MCTP message tag is out of range");
    }
    std::vector<Packet> packets;
    const auto packetCount =
        std::max<std::size_t>(1, (payload.size() + kPayloadMtu - 1) / kPayloadMtu);
    packets.reserve(packetCount);
    for (std::size_t index = 0; index < packetCount; ++index) {
        const auto begin = std::min(index * kPayloadMtu, payload.size());
        const auto end = std::min(begin + kPayloadMtu, payload.size());
        Packet packet;
        packet.destinationEid = destinationEid;
        packet.sourceEid = sourceEid;
        packet.som = index == 0;
        packet.eom = index + 1 == packetCount;
        packet.packetSequence = static_cast<std::uint8_t>(index % 4);
        packet.tagOwner = tagOwner;
        packet.messageTag = messageTag;
        packet.messageType = type;
        packet.payload.assign(payload.begin() + static_cast<std::ptrdiff_t>(begin),
                              payload.begin() + static_cast<std::ptrdiff_t>(end));
        packets.push_back(std::move(packet));
    }
    return packets;
}

} // namespace mctp
