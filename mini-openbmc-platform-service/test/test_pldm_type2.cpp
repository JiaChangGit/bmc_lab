#include "libs/common/byte_buffer.hpp"
#include "libs/pldm/pldm_type2.hpp"

#include <gtest/gtest.h>

namespace {
pldm::Type2Responder makeResponder() {
    pldm::PdrRepository repository;
    repository.add({1, 1, "GPU Core Temperature", "Cel", 85.0, -10.0});
    return pldm::Type2Responder(std::move(repository), {{1, 72.5}});
}
} // namespace

TEST(PldmType2, ReturnsRepositoryInfoAndPdr) {
    auto responder = makeResponder();
    auto info = responder.handle(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(pldm::PlatformCommand::getPdrRepositoryInfo)));
    ASSERT_EQ(info.completionCode, pldm::CompletionCode::success);
    common::ByteReader reader(info.payload);
    auto count = reader.readLe32();
    ASSERT_TRUE(count.ok());
    EXPECT_EQ(count.value(), 1U);

    std::vector<std::uint8_t> payload;
    common::appendLe32(payload, 1);
    auto pdr = responder.handle(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(pldm::PlatformCommand::getPdr), payload));
    ASSERT_EQ(pdr.completionCode, pldm::CompletionCode::success);
    ASSERT_GT(pdr.payload.size(), 4U);
    auto decoded = pldm::decodePdr(
        std::span<const std::uint8_t>(pdr.payload).subspan(4));
    ASSERT_TRUE(decoded.ok()) << decoded.status().message();
    EXPECT_EQ(decoded.value().name, "GPU Core Temperature");
}

TEST(PldmType2, ReturnsNumericSensorReading) {
    auto responder = makeResponder();
    std::vector<std::uint8_t> payload;
    common::appendLe16(payload, 1);
    auto response = responder.handle(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(pldm::PlatformCommand::getSensorReading),
        payload));
    auto reading = pldm::parseGetSensorReadingResponse(response);
    ASSERT_TRUE(reading.ok()) << reading.status().message();
    EXPECT_DOUBLE_EQ(reading.value().value, 72.5);
}

TEST(PldmType2, HandlesPlatformEventAndUnavailableSensor) {
    auto responder = makeResponder();
    auto event = responder.handle(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(pldm::PlatformCommand::platformEventMessage)));
    EXPECT_EQ(event.completionCode, pldm::CompletionCode::success);
    responder.setFault("sensor_unavailable");
    std::vector<std::uint8_t> payload;
    common::appendLe16(payload, 1);
    auto response = responder.handle(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(pldm::PlatformCommand::getSensorReading),
        payload));
    auto reading = pldm::parseGetSensorReadingResponse(response);
    ASSERT_TRUE(reading.ok());
    EXPECT_FALSE(reading.value().available);
}
