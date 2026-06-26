#include "libs/mctp/uds_mctp_transport.hpp"

#include "libs/mctp/mctp_packet.hpp"
#include "libs/mctp/mctp_reassembler.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace mctp {
namespace {

constexpr std::uint8_t kBmcEid = 1;
constexpr std::size_t kDatagramSize = kHeaderSize + kPayloadMtu;

common::Status configureAddress(const std::filesystem::path& path,
                                sockaddr_un& address) {
    const auto value = path.string();
    if (value.size() >= sizeof(address.sun_path)) {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "UDS path is too long");
    }
    address = {};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, value.c_str(), value.size() + 1);
    return common::Status::okStatus();
}

common::Status sendPackets(int fd, std::span<const std::uint8_t> payload,
                           std::uint8_t destination, std::uint8_t source,
                           std::uint8_t tag, bool tagOwner) {
    auto packets = fragmentMessage(payload, destination, source, MessageType::pldm,
                                   tag, tagOwner);
    if (!packets.ok()) return packets.status();
    for (const auto& packet : packets.value()) {
        auto bytes = encodePacket(packet);
        if (!bytes.ok()) return bytes.status();
        if (::send(fd, bytes.value().data(), bytes.value().size(), MSG_NOSIGNAL) !=
            static_cast<ssize_t>(bytes.value().size())) {
            return common::Status::error(common::StatusCode::ioError,
                                         "failed to send MCTP packet");
        }
    }
    return common::Status::okStatus();
}

common::Status sendPacketsWithBehavior(
    int fd, std::span<const std::uint8_t> payload, std::uint8_t destination,
    std::uint8_t source, std::uint8_t tag, bool tagOwner,
    ServerFaultBehavior behavior) {
    if (behavior == ServerFaultBehavior::none) {
        return sendPackets(fd, payload, destination, source, tag, tagOwner);
    }
    if (behavior == ServerFaultBehavior::packetLoss) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        return common::Status::okStatus();
    }
    std::vector<std::uint8_t> expanded(payload.begin(), payload.end());
    expanded.resize(kPayloadMtu + 8, 0);
    auto packets = fragmentMessage(expanded, destination, source,
                                   MessageType::pldm, tag, tagOwner);
    if (!packets.ok()) return packets.status();
    if (behavior == ServerFaultBehavior::outOfOrder) {
        std::swap(packets.value()[0], packets.value()[1]);
    } else if (behavior == ServerFaultBehavior::sequenceMismatch) {
        packets.value()[1].packetSequence =
            static_cast<std::uint8_t>((packets.value()[1].packetSequence + 1) % 4);
    } else if (behavior == ServerFaultBehavior::timeoutBeforeEom) {
        auto first = encodePacket(packets.value()[0]);
        if (!first.ok()) return first.status();
        if (::send(fd, first.value().data(), first.value().size(), MSG_NOSIGNAL) !=
            static_cast<ssize_t>(first.value().size())) {
            return common::Status::error(common::StatusCode::ioError,
                                         "failed to send MCTP fault packet");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        return common::Status::okStatus();
    }
    for (const auto& packet : packets.value()) {
        auto bytes = encodePacket(packet);
        if (!bytes.ok()) return bytes.status();
        if (::send(fd, bytes.value().data(), bytes.value().size(), MSG_NOSIGNAL) !=
            static_cast<ssize_t>(bytes.value().size())) {
            return common::Status::error(common::StatusCode::ioError,
                                         "failed to send MCTP fault packet");
        }
    }
    return common::Status::okStatus();
}

} // namespace

UdsMctpClient::UdsMctpClient(std::filesystem::path socketPath)
    : socketPath_(std::move(socketPath)) {}

common::StatusOr<std::vector<std::uint8_t>>
UdsMctpClient::request(std::span<const std::uint8_t> payload,
                       std::uint8_t destinationEid,
                       std::chrono::milliseconds timeout) {
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return common::Status::error(common::StatusCode::ioError,
                                     std::strerror(errno));
    }
    sockaddr_un address{};
    auto addressStatus = configureAddress(socketPath_, address);
    if (!addressStatus.ok()) {
        ::close(fd);
        return addressStatus;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        const std::string message = std::strerror(errno);
        ::close(fd);
        return common::Status::error(common::StatusCode::unavailable,
                                     "MCTP endpoint unavailable: " + message);
    }
    const auto tag = static_cast<std::uint8_t>(nextTag_++ & 0x07);
    auto sent = sendPackets(fd, payload, destinationEid, kBmcEid, tag, true);
    if (!sent.ok()) {
        ::close(fd);
        return sent;
    }
    Reassembler reassembler(timeout);
    std::array<std::uint8_t, kDatagramSize> buffer{};
    while (true) {
        pollfd descriptor{fd, POLLIN, 0};
        const int pollResult = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
        if (pollResult <= 0) {
            ::close(fd);
            return common::Status::error(
                pollResult == 0 ? common::StatusCode::timeout
                                : common::StatusCode::ioError,
                pollResult == 0 ? "MCTP response timed out" : std::strerror(errno));
        }
        const auto count = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (count <= 0) {
            ::close(fd);
            return common::Status::error(common::StatusCode::ioError,
                                         "MCTP response connection closed");
        }
        auto packet = decodePacket(
            std::span<const std::uint8_t>(buffer.data(), static_cast<std::size_t>(count)));
        if (!packet.ok()) {
            ::close(fd);
            return packet.status();
        }
        if (packet.value().messageTag != tag || packet.value().tagOwner) {
            ::close(fd);
            return common::Status::error(common::StatusCode::malformedData,
                                         "MCTP response tag did not match request");
        }
        auto completed = reassembler.addPacket(packet.value());
        if (!completed.ok()) {
            ::close(fd);
            return completed.status();
        }
        if (completed.value()) {
            ::close(fd);
            return std::move(*completed.value());
        }
    }
}

UdsMctpServer::UdsMctpServer(std::filesystem::path socketPath)
    : socketPath_(std::move(socketPath)) {}

UdsMctpServer::~UdsMctpServer() {
    stop();
    std::error_code ignored;
    std::filesystem::remove(socketPath_, ignored);
}

common::Status UdsMctpServer::run(const Handler& handler) {
    stopping_.store(false);
    std::filesystem::create_directories(socketPath_.parent_path());
    std::error_code ignored;
    std::filesystem::remove(socketPath_, ignored);
    const int listenFd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (listenFd < 0) {
        return common::Status::error(common::StatusCode::ioError,
                                     std::strerror(errno));
    }
    listenFd_.store(listenFd);
    sockaddr_un address{};
    auto addressStatus = configureAddress(socketPath_, address);
    if (!addressStatus.ok()) {
        stop();
        return addressStatus;
    }
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(listenFd, 8) < 0) {
        const std::string message = std::strerror(errno);
        stop();
        return common::Status::error(common::StatusCode::ioError,
                                     message);
    }
    while (!stopping_.load()) {
        const int client = ::accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            if (stopping_.load()) break;
            if (errno == EINTR) continue;
            stop();
            return common::Status::error(common::StatusCode::ioError,
                                         std::strerror(errno));
        }
        Reassembler reassembler;
        std::array<std::uint8_t, kDatagramSize> buffer{};
        while (true) {
            const auto count = ::recv(client, buffer.data(), buffer.size(), 0);
            if (count <= 0) break;
            auto packet = decodePacket(std::span<const std::uint8_t>(
                buffer.data(), static_cast<std::size_t>(count)));
            if (!packet.ok()) break;
            auto completed = reassembler.addPacket(packet.value());
            if (!completed.ok()) break;
            if (!completed.value()) continue;
            const auto behaviorBefore = faultBehavior_;
            auto response = handler(packet.value().sourceEid,
                                    packet.value().destinationEid,
                                    *completed.value());
            if (response.ok()) {
                const auto behavior =
                    behaviorBefore == faultBehavior_
                        ? behaviorBefore
                        : ServerFaultBehavior::none;
                (void)sendPacketsWithBehavior(
                    client, response.value(), packet.value().sourceEid,
                    packet.value().destinationEid, packet.value().messageTag,
                    false, behavior);
            }
            break;
        }
        ::close(client);
    }
    const int remainingFd = listenFd_.exchange(-1);
    if (remainingFd >= 0) ::close(remainingFd);
    return common::Status::okStatus();
}

void UdsMctpServer::setFaultBehavior(ServerFaultBehavior behavior) {
    faultBehavior_ = behavior;
}

void UdsMctpServer::stop() {
    stopping_.store(true);
    const int listenFd = listenFd_.exchange(-1);
    if (listenFd >= 0) {
        ::shutdown(listenFd, SHUT_RDWR);
        ::close(listenFd);
    }
}

} // namespace mctp
