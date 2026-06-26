#pragma once

#include "libs/common/status.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace mctp {

enum class ServerFaultBehavior {
    none,
    packetLoss,
    outOfOrder,
    sequenceMismatch,
    timeoutBeforeEom,
};

class UdsMctpClient {
  public:
    explicit UdsMctpClient(std::filesystem::path socketPath);
    common::StatusOr<std::vector<std::uint8_t>>
        request(std::span<const std::uint8_t> payload, std::uint8_t destinationEid,
                std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

  private:
    std::filesystem::path socketPath_;
    std::uint8_t nextTag_{};
};

class UdsMctpServer {
  public:
    using Handler = std::function<common::StatusOr<std::vector<std::uint8_t>>(
        std::uint8_t sourceEid, std::uint8_t destinationEid,
        std::span<const std::uint8_t>)>;

    explicit UdsMctpServer(std::filesystem::path socketPath);
    ~UdsMctpServer();
    common::Status run(const Handler& handler);
    void stop();
    void setFaultBehavior(ServerFaultBehavior behavior);

  private:
    std::filesystem::path socketPath_;
    std::atomic_int listenFd_{-1};
    std::atomic_bool stopping_{false};
    ServerFaultBehavior faultBehavior_{ServerFaultBehavior::none};
};

} // namespace mctp
