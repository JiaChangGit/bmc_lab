#include "libs/dbus/dbus_client.hpp"

#include <iostream>

int main() {
    dbus::DbusClient client;
    auto objects = client.listObjects();
    if (!objects.ok()) {
        std::cerr << "D-Bus dump failed: " << objects.status().message() << '\n';
        return 1;
    }
    for (const auto& [path, properties] : objects.value()) {
        std::cout << path << '\n';
        for (const auto& [name, value] : properties.items()) {
            std::cout << "  " << name << '=' << value.dump() << '\n';
        }
    }
    return 0;
}
