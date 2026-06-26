#include "libs/mctp/mctp_reassembler.hpp"

namespace mctp {

Reassembler::Reassembler(std::chrono::milliseconds timeout) : timeout_(timeout) {}

common::StatusOr<std::optional<std::vector<std::uint8_t>>>
Reassembler::addPacket(const Packet& packet,
                       std::chrono::steady_clock::time_point now) {
    if (active_ && now - lastPacket_ > timeout_) {
        reset();
        return common::Status::error(common::StatusCode::timeout,
                                     "MCTP reassembly timed out before EOM");
    }
    if (packet.som) {
        reset();
        active_ = true;
        expectedSequence_ = packet.packetSequence;
        messageTag_ = packet.messageTag;
        tagOwner_ = packet.tagOwner;
        messageType_ = packet.messageType;
    } else if (!active_) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "MCTP fragment arrived before SOM");
    }
    if (packet.messageTag != messageTag_ || packet.tagOwner != tagOwner_) {
        reset();
        return common::Status::error(common::StatusCode::malformedData,
                                     "MCTP tag mismatch");
    }
    if (packet.messageType != messageType_ ||
        packet.packetSequence != expectedSequence_) {
        reset();
        return common::Status::error(common::StatusCode::malformedData,
                                     "MCTP packet sequence mismatch");
    }
    payload_.insert(payload_.end(), packet.payload.begin(), packet.payload.end());
    lastPacket_ = now;
    expectedSequence_ = static_cast<std::uint8_t>((expectedSequence_ + 1) % 4);
    if (!packet.eom) {
        return std::optional<std::vector<std::uint8_t>>{};
    }
    auto completed = std::move(payload_);
    reset();
    return std::optional<std::vector<std::uint8_t>>(std::move(completed));
}

void Reassembler::reset() {
    active_ = false;
    expectedSequence_ = 0;
    messageTag_ = 0;
    tagOwner_ = false;
    payload_.clear();
}

} // namespace mctp
