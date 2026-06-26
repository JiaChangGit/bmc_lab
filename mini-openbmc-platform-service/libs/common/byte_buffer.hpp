#pragma once

#include "libs/common/status.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace common {

class ByteReader {
  public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    StatusOr<std::uint8_t> readU8() {
        if (offset_ >= bytes_.size()) {
            return Status::error(StatusCode::malformedData, "buffer underflow");
        }
        return bytes_[offset_++];
    }

    StatusOr<std::uint16_t> readLe16() {
        if (remaining() < 2) {
            return Status::error(StatusCode::malformedData, "buffer underflow");
        }
        const auto value = static_cast<std::uint16_t>(
            bytes_[offset_] | (static_cast<std::uint16_t>(bytes_[offset_ + 1]) << 8));
        offset_ += 2;
        return value;
    }

    StatusOr<std::uint32_t> readLe32() {
        if (remaining() < 4) {
            return Status::error(StatusCode::malformedData, "buffer underflow");
        }
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            value |= static_cast<std::uint32_t>(bytes_[offset_ + i]) << (i * 8);
        }
        offset_ += 4;
        return value;
    }

    StatusOr<std::vector<std::uint8_t>> readBytes(std::size_t count) {
        if (remaining() < count) {
            return Status::error(StatusCode::malformedData, "buffer underflow");
        }
        std::vector<std::uint8_t> result(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                         bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + count));
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - offset_; }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

inline void appendLe16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xff));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

inline void appendLe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

} // namespace common
