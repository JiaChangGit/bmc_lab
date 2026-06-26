#include "services/redfish-service/rest_server.hpp"

#include <csignal>

namespace {
service::RestServer* activeServer{};
void handleSignal(int) {
    if (activeServer) activeServer->stop();
}
} // namespace

int main() {
    service::RestServer server;
    activeServer = &server;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    return server.run();
}
