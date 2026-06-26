#include "libs/pldm/pldm_type0.hpp"

#include <gtest/gtest.h>

TEST(PldmType0, GetAndSetTid) {
    pldm::Type0Responder responder(8);
    auto get = pldm::makeRequest(
        pldm::Type::base,
        static_cast<std::uint8_t>(pldm::BaseCommand::getTid));
    auto getResponse = responder.handle(get);
    auto tid = pldm::parseGetTidResponse(getResponse);
    ASSERT_TRUE(tid.ok());
    EXPECT_EQ(tid.value(), 8);

    auto set = pldm::makeRequest(
        pldm::Type::base,
        static_cast<std::uint8_t>(pldm::BaseCommand::setTid), {12});
    EXPECT_EQ(responder.handle(set).completionCode, pldm::CompletionCode::success);
    EXPECT_EQ(responder.tid(), 12);
}

TEST(PldmType0, ReportsTypesAndCommands) {
    pldm::Type0Responder responder;
    auto types = responder.handle(pldm::makeRequest(
        pldm::Type::base,
        static_cast<std::uint8_t>(pldm::BaseCommand::getPldmTypes)));
    ASSERT_EQ(types.completionCode, pldm::CompletionCode::success);
    ASSERT_FALSE(types.payload.empty());
    EXPECT_NE(types.payload[0] & (1U << 2), 0);

    auto commands = responder.handle(pldm::makeRequest(
        pldm::Type::base,
        static_cast<std::uint8_t>(pldm::BaseCommand::getPldmCommands)));
    EXPECT_EQ(commands.completionCode, pldm::CompletionCode::success);
    EXPECT_EQ(commands.payload.size(), 4U);
}

TEST(PldmType0, ParserRejectsBadCompletionCode) {
    auto response = pldm::makeResponse(
        pldm::makeRequest(pldm::Type::base,
                          static_cast<std::uint8_t>(pldm::BaseCommand::getTid)),
        pldm::CompletionCode::error);
    EXPECT_FALSE(pldm::parseGetTidResponse(response).ok());
}
