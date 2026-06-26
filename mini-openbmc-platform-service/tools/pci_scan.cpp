#include "libs/pcie/pci_sysfs_reader.hpp"

#include <iostream>

int main(int argc, char** argv) {
    const std::filesystem::path root =
        argc > 1 ? argv[1] : "/sys/bus/pci/devices";
    pcie::PciSysfsReader reader(root);
    auto devices = reader.scan();
    if (!devices.ok()) {
        std::cerr << "PCI scan failed: " << devices.status().message() << '\n';
        return 1;
    }
    for (const auto& device : devices.value()) {
        std::cout << "BDF=" << device.bdf
                  << " vendor=" << device.vendor.value_or("unknown")
                  << " device=" << device.device.value_or("unknown")
                  << " class=" << device.classCode.value_or("unknown")
                  << " driver=" << device.driver.value_or("unbound")
                  << " width=";
        if (device.currentLinkWidth) std::cout << 'x' << *device.currentLinkWidth;
        else std::cout << "unknown";
        std::cout << " speed=" << device.currentLinkSpeed.value_or("unknown")
                  << '\n';
    }
    return 0;
}
