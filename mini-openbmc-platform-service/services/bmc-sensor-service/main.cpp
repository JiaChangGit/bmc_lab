#include "services/bmc-sensor-service/app.hpp"

#include <csignal>
#include <iostream>

namespace {
service::SensorServiceApp* activeApp{};
void handleSignal(int) {
    if (activeApp) activeApp->stop();
}
} // namespace

int main() {
    service::SensorServiceApp app;
    activeApp = &app;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::cout << "BMC sensor service starting on the D-Bus session bus\n";
    const auto status = app.run();
    if (!status.ok()) {
        std::cerr << "BMC sensor service failed: " << status.message() << '\n';
        return 1;
    }
    return 0;
}
