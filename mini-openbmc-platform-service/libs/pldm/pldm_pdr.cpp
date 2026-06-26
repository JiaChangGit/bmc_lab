#include "libs/pldm/pldm_pdr.hpp"

#include "libs/common/byte_buffer.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace pldm {
namespace {

void appendDouble(std::vector<std::uint8_t>& out, double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    const auto raw = std::bit_cast<std::uint64_t>(value);
    for (std::size_t i = 0; i < sizeof(raw); ++i) {
        out.push_back(static_cast<std::uint8_t>((raw >> (8 * i)) & 0xff));
    }
}

common::StatusOr<double> readDouble(common::ByteReader& reader) {
    auto bytes = reader.readBytes(sizeof(double));
    if (!bytes.ok()) return bytes.status();
    std::uint64_t raw{};
    for (std::size_t i = 0; i < bytes.value().size(); ++i) {
        raw |= static_cast<std::uint64_t>(bytes.value()[i]) << (8 * i);
    }
    return std::bit_cast<double>(raw);
}

} // namespace

common::StatusOr<std::vector<std::uint8_t>> encodePdr(const NumericSensorPdr& pdr) {
    if (pdr.name.size() > 255 || pdr.unit.size() > 255) {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "PDR string is too long");
    }
    std::vector<std::uint8_t> bytes;
    common::appendLe32(bytes, pdr.recordHandle);
    common::appendLe16(bytes, pdr.sensorId);
    bytes.push_back(static_cast<std::uint8_t>(pdr.name.size()));
    bytes.insert(bytes.end(), pdr.name.begin(), pdr.name.end());
    bytes.push_back(static_cast<std::uint8_t>(pdr.unit.size()));
    bytes.insert(bytes.end(), pdr.unit.begin(), pdr.unit.end());
    appendDouble(bytes, pdr.upperCritical);
    appendDouble(bytes, pdr.lowerCritical);
    return bytes;
}

common::StatusOr<NumericSensorPdr> decodePdr(std::span<const std::uint8_t> bytes) {
    common::ByteReader reader(bytes);
    auto handle = reader.readLe32();
    auto sensorId = reader.readLe16();
    auto nameLength = reader.readU8();
    if (!handle.ok()) return handle.status();
    if (!sensorId.ok()) return sensorId.status();
    if (!nameLength.ok()) return nameLength.status();
    auto name = reader.readBytes(nameLength.value());
    if (!name.ok()) return name.status();
    auto unitLength = reader.readU8();
    if (!unitLength.ok()) return unitLength.status();
    auto unit = reader.readBytes(unitLength.value());
    if (!unit.ok()) return unit.status();
    auto upper = readDouble(reader);
    auto lower = readDouble(reader);
    if (!upper.ok()) return upper.status();
    if (!lower.ok()) return lower.status();
    if (reader.remaining() != 0) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "PDR has trailing bytes");
    }
    return NumericSensorPdr{
        handle.value(), sensorId.value(),
        std::string(name.value().begin(), name.value().end()),
        std::string(unit.value().begin(), unit.value().end()),
        upper.value(), lower.value()};
}

void PdrRepository::add(NumericSensorPdr pdr) {
    records_.push_back(std::move(pdr));
    std::sort(records_.begin(), records_.end(),
              [](const auto& left, const auto& right) {
                  return left.recordHandle < right.recordHandle;
              });
}

const std::vector<NumericSensorPdr>& PdrRepository::records() const {
    return records_;
}

const NumericSensorPdr* PdrRepository::find(std::uint32_t handle) const {
    const auto iterator =
        std::find_if(records_.begin(), records_.end(),
                     [handle](const auto& pdr) { return pdr.recordHandle == handle; });
    return iterator == records_.end() ? nullptr : &*iterator;
}

std::uint32_t PdrRepository::nextHandle(std::uint32_t handle) const {
    for (const auto& record : records_) {
        if (record.recordHandle > handle) return record.recordHandle;
    }
    return 0;
}

} // namespace pldm
