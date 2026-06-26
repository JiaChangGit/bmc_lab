#include "services/pldm-endpoint-agent/nic_endpoint.hpp"

namespace service {
namespace {

pldm::PdrRepository nicRepository() {
    pldm::PdrRepository repository;
    repository.add({101, 101, "NIC Temperature", "Cel", 80.0, -10.0});
    repository.add({102, 102, "NIC Link Status", "State", 1.0, 0.5});
    repository.add({103, 103, "NIC Correctable Error Count", "Count", 100.0, 0.0});
    repository.add({104, 104, "NIC Packet Error Count", "Count", 1000.0, 0.0});
    return repository;
}

} // namespace

NicEndpoint::NicEndpoint()
    : responder_(nicRepository(),
                 {{101, 48.0}, {102, 1.0}, {103, 0.0}, {104, 0.0}}) {}

pldm::Message NicEndpoint::handle(const pldm::Message& request) {
    return responder_.handle(request);
}

void NicEndpoint::setFault(const std::string& fault) {
    responder_.setFault(fault);
}

} // namespace service
