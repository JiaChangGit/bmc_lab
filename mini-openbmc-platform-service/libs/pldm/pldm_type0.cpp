#include "libs/pldm/pldm_type0.hpp"

namespace pldm {

Type0Responder::Type0Responder(std::uint8_t tid) : tid_(tid) {}

Message Type0Responder::handle(const Message& request) {
    if (!request.header.request || request.header.type != Type::base) {
        return makeResponse(request, CompletionCode::invalidData);
    }
    switch (static_cast<BaseCommand>(request.header.command)) {
    case BaseCommand::getTid:
        return makeResponse(request, CompletionCode::success, {tid_});
    case BaseCommand::setTid:
        if (request.payload.size() != 1) {
            return makeResponse(request, CompletionCode::invalidData);
        }
        tid_ = request.payload[0];
        return makeResponse(request, CompletionCode::success);
    case BaseCommand::getPldmVersion:
        return makeResponse(request, CompletionCode::success, {1, 0, 0, 0});
    case BaseCommand::getPldmTypes:
        return makeResponse(request, CompletionCode::success,
                            {static_cast<std::uint8_t>((1U << 0) | (1U << 2))});
    case BaseCommand::getPldmCommands:
        return makeResponse(request, CompletionCode::success, {0x3e, 0, 0, 0});
    default:
        return makeResponse(request, CompletionCode::invalidCommand);
    }
}

common::StatusOr<std::uint8_t> parseGetTidResponse(const Message& response) {
    if (response.header.request ||
        response.header.command != static_cast<std::uint8_t>(BaseCommand::getTid) ||
        response.completionCode != CompletionCode::success ||
        response.payload.size() != 1) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "invalid PLDM GetTID response");
    }
    return response.payload[0];
}

} // namespace pldm
