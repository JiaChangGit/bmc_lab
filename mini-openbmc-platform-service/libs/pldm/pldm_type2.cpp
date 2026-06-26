#include "libs/pldm/pldm_type2.hpp"

#include "libs/common/byte_buffer.hpp"

#include <bit>

namespace pldm {
namespace {

std::vector<std::uint8_t> encodeDouble(double value) {
    const auto raw = std::bit_cast<std::uint64_t>(value);
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < sizeof(raw); ++i) {
        bytes.push_back(static_cast<std::uint8_t>((raw >> (8 * i)) & 0xff));
    }
    return bytes;
}

common::StatusOr<double> decodeDouble(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != sizeof(double)) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "numeric reading size mismatch");
    }
    std::uint64_t raw{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        raw |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }
    return std::bit_cast<double>(raw);
}

} // namespace

Type2Responder::Type2Responder(
    PdrRepository repository,
    std::unordered_map<std::uint16_t, double> readings)
    : repository_(std::move(repository)), readings_(std::move(readings)) {}

Message Type2Responder::handle(const Message& request) {
    if (!request.header.request || request.header.type != Type::platform) {
        return makeResponse(request, CompletionCode::invalidData);
    }
    if (fault_ == "unsupported_command") {
        return makeResponse(request, CompletionCode::invalidCommand);
    }
    if (fault_ == "bad_completion_code") {
        return makeResponse(request, CompletionCode::error);
    }
    switch (static_cast<PlatformCommand>(request.header.command)) {
    case PlatformCommand::getPdrRepositoryInfo: {
        std::vector<std::uint8_t> payload;
        common::appendLe32(payload,
                           static_cast<std::uint32_t>(repository_.records().size()));
        return makeResponse(request, CompletionCode::success, std::move(payload));
    }
    case PlatformCommand::getPdr: {
        if (request.payload.size() != 4) {
            return makeResponse(request, CompletionCode::invalidData);
        }
        common::ByteReader reader(request.payload);
        auto handle = reader.readLe32();
        const NumericSensorPdr* pdr = nullptr;
        if (handle.ok() && handle.value() == 0 &&
            !repository_.records().empty()) {
            pdr = &repository_.records().front();
        } else if (handle.ok()) {
            pdr = repository_.find(handle.value());
        }
        if (!pdr) return makeResponse(request, CompletionCode::invalidData);
        auto encoded = encodePdr(*pdr);
        if (!encoded.ok()) return makeResponse(request, CompletionCode::error);
        std::vector<std::uint8_t> payload;
        common::appendLe32(payload, repository_.nextHandle(pdr->recordHandle));
        payload.insert(payload.end(), encoded.value().begin(), encoded.value().end());
        return makeResponse(request, CompletionCode::success, std::move(payload));
    }
    case PlatformCommand::getSensorReading: {
        if (request.payload.size() != 2) {
            return makeResponse(request, CompletionCode::invalidData);
        }
        common::ByteReader reader(request.payload);
        auto sensorId = reader.readLe16();
        if (!sensorId.ok()) return makeResponse(request, CompletionCode::invalidData);
        if (fault_ == "sensor_unavailable") {
            return makeResponse(request, CompletionCode::unavailable);
        }
        const auto reading = readings_.find(sensorId.value());
        if (reading == readings_.end()) {
            return makeResponse(request, CompletionCode::invalidData);
        }
        std::vector<std::uint8_t> payload;
        common::appendLe16(payload, sensorId.value());
        const auto value = encodeDouble(reading->second);
        payload.insert(payload.end(), value.begin(), value.end());
        return makeResponse(request, CompletionCode::success, std::move(payload));
    }
    case PlatformCommand::setEventReceiver:
    case PlatformCommand::platformEventMessage:
        return makeResponse(request, CompletionCode::success);
    case PlatformCommand::setFault:
        fault_.assign(request.payload.begin(), request.payload.end());
        return makeResponse(request, CompletionCode::success);
    default:
        return makeResponse(request, CompletionCode::invalidCommand);
    }
}

void Type2Responder::setReading(std::uint16_t sensorId, double value) {
    readings_[sensorId] = value;
}

void Type2Responder::setFault(std::string fault) { fault_ = std::move(fault); }

common::StatusOr<NumericReading>
parseGetSensorReadingResponse(const Message& response) {
    if (response.header.request ||
        response.header.command !=
            static_cast<std::uint8_t>(PlatformCommand::getSensorReading)) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "not a GetSensorReading response");
    }
    if (response.completionCode == CompletionCode::unavailable) {
        return NumericReading{0, 0.0, false};
    }
    if (response.completionCode != CompletionCode::success ||
        response.payload.size() != 10) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "GetSensorReading response is invalid");
    }
    common::ByteReader reader(response.payload);
    auto sensorId = reader.readLe16();
    auto raw = reader.readBytes(sizeof(double));
    if (!sensorId.ok()) return sensorId.status();
    if (!raw.ok()) return raw.status();
    auto value = decodeDouble(raw.value());
    if (!value.ok()) return value.status();
    return NumericReading{sensorId.value(), value.value(), true};
}

} // namespace pldm
