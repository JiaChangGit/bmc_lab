#include "libs/pldm/pldm_common.hpp"

namespace pldm {
namespace {
constexpr std::uint8_t kRequestMask = 0x80;
constexpr std::uint8_t kDatagramMask = 0x40;
constexpr std::uint8_t kInstanceMask = 0x1f;
constexpr std::uint8_t kTypeMask = 0x3f;
} // namespace

common::StatusOr<std::vector<std::uint8_t>> encode(const Message& message) {
    if (message.header.instanceId > kInstanceMask ||
        (message.header.type != Type::base && message.header.type != Type::platform)) {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "PLDM header field is out of range");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(4 + message.payload.size());
    bytes.push_back(static_cast<std::uint8_t>(
        (message.header.request ? kRequestMask : 0) |
        (message.header.datagram ? kDatagramMask : 0) |
        (message.header.instanceId & kInstanceMask)));
    bytes.push_back(static_cast<std::uint8_t>(message.header.type));
    bytes.push_back(message.header.command);
    if (!message.header.request) {
        bytes.push_back(static_cast<std::uint8_t>(message.completionCode));
    }
    bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
    return bytes;
}

common::StatusOr<Message> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 3) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "PLDM message is shorter than its header");
    }
    const auto rawType = static_cast<std::uint8_t>(bytes[1] & kTypeMask);
    if (rawType != static_cast<std::uint8_t>(Type::base) &&
        rawType != static_cast<std::uint8_t>(Type::platform)) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "unsupported PLDM type");
    }
    Message message;
    message.header.instanceId = static_cast<std::uint8_t>(bytes[0] & kInstanceMask);
    message.header.request = (bytes[0] & kRequestMask) != 0;
    message.header.datagram = (bytes[0] & kDatagramMask) != 0;
    message.header.type = static_cast<Type>(rawType);
    message.header.command = bytes[2];
    std::size_t payloadOffset = 3;
    if (!message.header.request) {
        if (bytes.size() < 4) {
            return common::Status::error(common::StatusCode::malformedData,
                                         "PLDM response has no completion code");
        }
        message.completionCode = static_cast<CompletionCode>(bytes[3]);
        payloadOffset = 4;
    }
    message.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(payloadOffset),
                           bytes.end());
    return message;
}

Message makeRequest(Type type, std::uint8_t command,
                    std::vector<std::uint8_t> payload, std::uint8_t instanceId) {
    return Message{Header{instanceId, true, false, type, command},
                   CompletionCode::success, std::move(payload)};
}

Message makeResponse(const Message& request, CompletionCode completion,
                     std::vector<std::uint8_t> payload) {
    return Message{Header{request.header.instanceId, false, false,
                          request.header.type, request.header.command},
                   completion, std::move(payload)};
}

} // namespace pldm
