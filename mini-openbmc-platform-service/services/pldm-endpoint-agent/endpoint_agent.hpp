#pragma once

#include "libs/common/logger.hpp"
#include "libs/mctp/uds_mctp_transport.hpp"
#include "libs/pldm/pldm_type0.hpp"
#include "services/pldm-endpoint-agent/gpu_endpoint.hpp"
#include "services/pldm-endpoint-agent/nic_endpoint.hpp"

#include <filesystem>
#include <span>

namespace service {

class EndpointAgent {
  public:
    explicit EndpointAgent(std::filesystem::path socketPath);
    common::Status run();
    void stop();

  private:
    common::StatusOr<std::vector<std::uint8_t>>
        handle(std::uint8_t sourceEid, std::uint8_t destinationEid,
               std::span<const std::uint8_t> request);

    mctp::UdsMctpServer server_;
    pldm::Type0Responder gpuBaseResponder_;
    pldm::Type0Responder nicBaseResponder_;
    GpuEndpoint gpuEndpoint_;
    NicEndpoint nicEndpoint_;
    common::JsonLogger logger_;
    std::string transportFault_;
};

} // namespace service
