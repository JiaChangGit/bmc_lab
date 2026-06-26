#include "libs/mctp/mctp_reassembler.hpp"
#include "libs/mctp/uds_mctp_transport.hpp"

#include <filesystem>
#include <gtest/gtest.h>
#include <numeric>
#include <thread>
#include <unistd.h>

TEST(MctpFragmentation, ReassemblesPayloadLargerThanMtu) {
    std::vector<std::uint8_t> payload(150);
    std::iota(payload.begin(), payload.end(), 0);
    auto packets = mctp::fragmentMessage(payload, 8, 1, mctp::MessageType::pldm,
                                         3, true);
    ASSERT_TRUE(packets.ok());
    ASSERT_EQ(packets.value().size(), 3U);
    mctp::Reassembler reassembler;
    std::optional<std::vector<std::uint8_t>> completed;
    for (const auto& packet : packets.value()) {
        auto result = reassembler.addPacket(packet);
        ASSERT_TRUE(result.ok()) << result.status().message();
        completed = result.value();
    }
    ASSERT_TRUE(completed);
    EXPECT_EQ(*completed, payload);
}

TEST(MctpFragmentation, DetectsSequenceAndTagMismatch) {
    std::vector<std::uint8_t> payload(100, 7);
    auto packets = mctp::fragmentMessage(payload, 8, 1, mctp::MessageType::pldm,
                                         3, true);
    ASSERT_TRUE(packets.ok());
    mctp::Reassembler reassembler;
    ASSERT_TRUE(reassembler.addPacket(packets.value()[0]).ok());
    auto badSequence = packets.value()[1];
    badSequence.packetSequence = 3;
    EXPECT_FALSE(reassembler.addPacket(badSequence).ok());

    mctp::Reassembler tagReassembler;
    ASSERT_TRUE(tagReassembler.addPacket(packets.value()[0]).ok());
    auto badTag = packets.value()[1];
    badTag.messageTag = 4;
    EXPECT_FALSE(tagReassembler.addPacket(badTag).ok());
}

TEST(MctpFragmentation, DetectsTimeoutBeforeEom) {
    std::vector<std::uint8_t> payload(100, 7);
    auto packets = mctp::fragmentMessage(payload, 8, 1, mctp::MessageType::pldm,
                                         3, true);
    ASSERT_TRUE(packets.ok());
    mctp::Reassembler reassembler(std::chrono::milliseconds(10));
    const auto start = std::chrono::steady_clock::now();
    ASSERT_TRUE(reassembler.addPacket(packets.value()[0], start).ok());
    auto result = reassembler.addPacket(
        packets.value()[1], start + std::chrono::milliseconds(20));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), common::StatusCode::timeout);
}

TEST(MctpFragmentation, UdsServerInjectsSequenceMismatch) {
    const auto socketPath =
        std::filesystem::temp_directory_path() /
        ("mini-mctp-fault-" + std::to_string(::getpid()) + ".sock");
    mctp::UdsMctpServer server(socketPath);
    server.setFaultBehavior(mctp::ServerFaultBehavior::sequenceMismatch);
    std::thread serverThread([&] {
        (void)server.run(
            [](std::uint8_t, std::uint8_t,
               std::span<const std::uint8_t> payload)
                -> common::StatusOr<std::vector<std::uint8_t>> {
                return std::vector<std::uint8_t>(payload.begin(), payload.end());
            });
    });
    for (int attempt = 0;
         attempt < 100 && !std::filesystem::exists(socketPath); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    mctp::UdsMctpClient client(socketPath);
    const auto result =
        client.request(std::vector<std::uint8_t>{1, 2, 3}, 8,
                       std::chrono::milliseconds(500));
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status().code(), common::StatusCode::malformedData);
    server.stop();
    serverThread.join();
    std::filesystem::remove(socketPath);
}
