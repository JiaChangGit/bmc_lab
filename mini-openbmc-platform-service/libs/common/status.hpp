#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace common {

enum class StatusCode {
    ok,
    invalidArgument,
    notFound,
    unavailable,
    timeout,
    malformedData,
    ioError,
    internalError,
};

class Status {
  public:
    Status() = default;
    Status(StatusCode code, std::string message)
        : code_(code), message_(std::move(message)) {}

    static Status okStatus() { return {}; }
    static Status error(StatusCode code, std::string message) {
        return {code, std::move(message)};
    }

    [[nodiscard]] bool ok() const { return code_ == StatusCode::ok; }
    [[nodiscard]] StatusCode code() const { return code_; }
    [[nodiscard]] const std::string& message() const { return message_; }

  private:
    StatusCode code_{StatusCode::ok};
    std::string message_;
};

template <typename T> class StatusOr {
  public:
    StatusOr(T value) : value_(std::move(value)) {}
    StatusOr(Status status) : status_(std::move(status)) {
        if (status_.ok()) {
            throw std::invalid_argument("StatusOr error status must not be OK");
        }
    }

    [[nodiscard]] bool ok() const { return value_.has_value(); }
    [[nodiscard]] const Status& status() const { return status_; }
    [[nodiscard]] const T& value() const& {
        if (!value_) {
            throw std::logic_error("StatusOr has no value");
        }
        return *value_;
    }
    [[nodiscard]] T& value() & {
        if (!value_) {
            throw std::logic_error("StatusOr has no value");
        }
        return *value_;
    }
    [[nodiscard]] T&& value() && {
        if (!value_) {
            throw std::logic_error("StatusOr has no value");
        }
        return std::move(*value_);
    }

  private:
    std::optional<T> value_;
    Status status_;
};

} // namespace common
