#pragma once

#include "libs/common/status.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace pldm {

enum class Type : std::uint8_t { base = 0, platform = 2 };
enum class CompletionCode : std::uint8_t {
    success = 0x00,
    error = 0x01,
    invalidData = 0x02,
    invalidCommand = 0x05,
    unavailable = 0x80,
};

struct Header {
    std::uint8_t instanceId{};
    bool request{};
    bool datagram{};
    Type type{Type::base};
    std::uint8_t command{};
};

struct Message {
    Header header;
    CompletionCode completionCode{CompletionCode::success};
    std::vector<std::uint8_t> payload;
};

common::StatusOr<std::vector<std::uint8_t>> encode(const Message& message);
common::StatusOr<Message> decode(std::span<const std::uint8_t> bytes);
Message makeRequest(Type type, std::uint8_t command,
                    std::vector<std::uint8_t> payload = {},
                    std::uint8_t instanceId = 0);
Message makeResponse(const Message& request, CompletionCode completion,
                     std::vector<std::uint8_t> payload = {});

} // namespace pldm
