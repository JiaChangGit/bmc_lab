#include "libs/common/byte_buffer.hpp"
#include "libs/mctp/uds_mctp_transport.hpp"
#include "libs/pldm/pldm_common.hpp"
#include "libs/pldm/pldm_type0.hpp"
#include "libs/pldm/pldm_type2.hpp"

#include <iostream>

int main(int argc, char** argv) {
    const std::filesystem::path socket =
        argc > 1 ? argv[1] : "runtime/sockets/mctp_endpoint.sock";
    mctp::UdsMctpClient client(socket);
    auto exchange = [&client](const pldm::Message& request)
        -> common::StatusOr<pldm::Message> {
        auto encoded = pldm::encode(request);
        if (!encoded.ok()) return encoded.status();
        auto raw = client.request(encoded.value(), 8);
        if (!raw.ok()) return raw.status();
        auto response = pldm::decode(raw.value());
        if (!response.ok()) return response.status();
        return response.value();
    };

    auto tidResponse = exchange(pldm::makeRequest(
        pldm::Type::base,
        static_cast<std::uint8_t>(pldm::BaseCommand::getTid)));
    if (!tidResponse.ok()) {
        std::cerr << "PLDM GetTID exchange failed: "
                  << tidResponse.status().message() << '\n';
        return 1;
    }
    auto tid = pldm::parseGetTidResponse(tidResponse.value());
    if (!tid.ok()) {
        std::cerr << "GetTID response validation failed: "
                  << tid.status().message() << '\n';
        return 1;
    }
    std::cout << "PLDM GetTID response: TID=" << static_cast<int>(tid.value())
              << " via UDS-MCTP\n";

    auto repositoryInfo = exchange(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(
            pldm::PlatformCommand::getPdrRepositoryInfo)));
    if (!repositoryInfo.ok() ||
        repositoryInfo.value().completionCode != pldm::CompletionCode::success) {
        std::cerr << "PLDM GetPDRRepositoryInfo failed\n";
        return 1;
    }
    common::ByteReader repositoryReader(repositoryInfo.value().payload);
    auto recordCount = repositoryReader.readLe32();
    if (!recordCount.ok()) {
        std::cerr << "PLDM repository response is malformed\n";
        return 1;
    }
    std::cout << "PLDM PDR repository records: " << recordCount.value() << '\n';

    std::vector<std::uint8_t> readingPayload;
    common::appendLe16(readingPayload, 1);
    auto readingResponse = exchange(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(pldm::PlatformCommand::getSensorReading),
        std::move(readingPayload)));
    if (!readingResponse.ok()) {
        std::cerr << "PLDM GetSensorReading exchange failed: "
                  << readingResponse.status().message() << '\n';
        return 1;
    }
    auto reading =
        pldm::parseGetSensorReadingResponse(readingResponse.value());
    if (!reading.ok() || !reading.value().available) {
        std::cerr << "PLDM sensor 1 is unavailable\n";
        return 1;
    }
    std::cout << "PLDM sensor 1 reading: " << reading.value().value << " Cel\n";

    auto eventResponse = exchange(pldm::makeRequest(
        pldm::Type::platform,
        static_cast<std::uint8_t>(
            pldm::PlatformCommand::platformEventMessage),
        {1, 0}));
    if (!eventResponse.ok() ||
        eventResponse.value().completionCode != pldm::CompletionCode::success) {
        std::cerr << "PLDM PlatformEventMessage failed\n";
        return 1;
    }
    std::cout << "PLDM PlatformEventMessage accepted\n";
    return 0;
}
