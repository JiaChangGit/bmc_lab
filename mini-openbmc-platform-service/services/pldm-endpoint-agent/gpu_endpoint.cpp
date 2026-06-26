#include "services/pldm-endpoint-agent/gpu_endpoint.hpp"

namespace service {
namespace {

pldm::PdrRepository gpuRepository() {
    pldm::PdrRepository repository;
    repository.add({1, 1, "GPU Core Temperature", "Cel", 85.0, -10.0});
    repository.add({2, 2, "GPU Power", "W", 320.0, 0.0});
    repository.add({3, 3, "PCIe Correctable Error Count", "Count", 100.0, 0.0});
    repository.add({4, 4, "PCIe Link Status", "State", 1.0, 0.5});
    return repository;
}

} // namespace

GpuEndpoint::GpuEndpoint()
    : responder_(gpuRepository(), {{1, 65.0}, {2, 250.0}, {3, 0.0}, {4, 1.0}}) {}

pldm::Message GpuEndpoint::handle(const pldm::Message& request) {
    return responder_.handle(request);
}

void GpuEndpoint::setFault(const std::string& fault) {
    responder_.setFault(fault);
}

} // namespace service
