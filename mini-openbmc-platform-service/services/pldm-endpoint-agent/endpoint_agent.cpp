#include "services/pldm-endpoint-agent/endpoint_agent.hpp"

#include "libs/pldm/pldm_common.hpp"
#include "libs/pldm/pldm_type2.hpp"

#include <algorithm>

namespace service {
namespace {
constexpr std::uint8_t kGpuEid = 8;
constexpr std::uint8_t kNicEid = 9;
} // namespace

EndpointAgent::EndpointAgent(std::filesystem::path socketPath)
    : server_(std::move(socketPath)), gpuBaseResponder_(kGpuEid),
      nicBaseResponder_(kNicEid),
      logger_("runtime/logs/mini-openbmc-service.jsonl") {}

common::Status EndpointAgent::run() {
    logger_.log("INFO", "pldm-endpoint-agent", "Endpoint agent started",
                {{"service", "EndpointAgent"}});
    return server_.run([this](std::uint8_t sourceEid,
                             std::uint8_t destinationEid,
                             std::span<const std::uint8_t> request) {
        return handle(sourceEid, destinationEid, request);
    });
}

void EndpointAgent::stop() { server_.stop(); }

common::StatusOr<std::vector<std::uint8_t>>
EndpointAgent::handle(std::uint8_t sourceEid,
                      std::uint8_t destinationEid,
                      std::span<const std::uint8_t> requestBytes) {
    (void)sourceEid;
    auto request = pldm::decode(requestBytes);
    if (!request.ok()) return request.status();

    const bool faultControl =
        request.value().header.type == pldm::Type::platform &&
        request.value().header.command ==
            static_cast<std::uint8_t>(pldm::PlatformCommand::setFault);
    if (transportFault_ == "malformed_response" && !faultControl) {
        return std::vector<std::uint8_t>{0xff, 0x00};
    }

    if (faultControl) {
        const std::string fault(request.value().payload.begin(),
                                request.value().payload.end());
        static const std::vector<std::string> transportFaults{
            "packet_loss", "out_of_order_packet", "sequence_mismatch",
            "timeout_before_eom", "malformed_response"};
        if (std::find(transportFaults.begin(), transportFaults.end(), fault) !=
            transportFaults.end()) {
            transportFault_ = fault;
            if (fault == "packet_loss") {
                server_.setFaultBehavior(mctp::ServerFaultBehavior::packetLoss);
            } else if (fault == "out_of_order_packet") {
                server_.setFaultBehavior(mctp::ServerFaultBehavior::outOfOrder);
            } else if (fault == "sequence_mismatch") {
                server_.setFaultBehavior(
                    mctp::ServerFaultBehavior::sequenceMismatch);
            } else if (fault == "timeout_before_eom") {
                server_.setFaultBehavior(
                    mctp::ServerFaultBehavior::timeoutBeforeEom);
            } else {
                server_.setFaultBehavior(mctp::ServerFaultBehavior::none);
            }
        } else {
            transportFault_.clear();
            server_.setFaultBehavior(mctp::ServerFaultBehavior::none);
            gpuEndpoint_.setFault(fault);
            nicEndpoint_.setFault(fault);
        }
    }

    pldm::Message response;
    if (request.value().header.type == pldm::Type::base) {
        response = destinationEid == kNicEid
                       ? nicBaseResponder_.handle(request.value())
                       : gpuBaseResponder_.handle(request.value());
    } else if (request.value().header.type == pldm::Type::platform) {
        response = destinationEid == kNicEid
                       ? nicEndpoint_.handle(request.value())
                       : gpuEndpoint_.handle(request.value());
    } else {
        response = pldm::makeResponse(request.value(),
                                      pldm::CompletionCode::invalidCommand);
    }
    logger_.log("DEBUG", "pldm-endpoint-agent", "PLDM command processed",
                {{"service", "EndpointAgent"},
                 {"eid", destinationEid},
                 {"pldmType", static_cast<int>(request.value().header.type)},
                 {"pldmCommand", request.value().header.command}});
    return pldm::encode(response);
}

} // namespace service
