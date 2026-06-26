#include "services/pldm-endpoint-agent/endpoint_agent.hpp"

#include <csignal>
#include <iostream>

namespace {
service::EndpointAgent* activeAgent{};
void handleSignal(int) {
    if (activeAgent) activeAgent->stop();
}
} // namespace

int main() {
    service::EndpointAgent agent("runtime/sockets/mctp_endpoint.sock");
    activeAgent = &agent;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::cout << "PLDM endpoint agent listening on runtime/sockets/mctp_endpoint.sock\n";
    const auto status = agent.run();
    if (!status.ok()) {
        std::cerr << "Endpoint agent failed: " << status.message() << '\n';
        return 1;
    }
    return 0;
}
