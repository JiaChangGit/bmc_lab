#include "libs/mctp/mctp_packet.hpp"

#include <gtest/gtest.h>

TEST(MctpPacket, EncodesAndDecodesHeader) {
    mctp::Packet packet{mctp::kHeaderVersion, 8, 1, true, true, 2, true, 5,
                        mctp::MessageType::pldm, {1, 2, 3}};
    auto encoded = mctp::encodePacket(packet);
    ASSERT_TRUE(encoded.ok());
    auto decoded = mctp::decodePacket(encoded.value());
    ASSERT_TRUE(decoded.ok()) << decoded.status().message();
    EXPECT_EQ(decoded.value().destinationEid, 8);
    EXPECT_EQ(decoded.value().packetSequence, 2);
    EXPECT_EQ(decoded.value().messageTag, 5);
    EXPECT_EQ(decoded.value().payload, packet.payload);
}

TEST(MctpPacket, RejectsShortAndMalformedPackets) {
    EXPECT_FALSE(mctp::decodePacket(std::vector<std::uint8_t>{1, 2}).ok());
    std::vector<std::uint8_t> unknown{1, 8, 1, 0xc0, 0xff, 0};
    EXPECT_FALSE(mctp::decodePacket(unknown).ok());
    std::vector<std::uint8_t> wrongLength{1, 8, 1, 0xc0, 1, 2, 0};
    EXPECT_FALSE(mctp::decodePacket(wrongLength).ok());
}

TEST(MctpPacket, RejectsOversizedPayload) {
    mctp::Packet packet;
    packet.payload.resize(mctp::kPayloadMtu + 1);
    EXPECT_FALSE(mctp::encodePacket(packet).ok());
}
