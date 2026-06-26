#include "libs/pcie/mini_pcie_ioctl.hpp"

#include <sstream>

namespace pcie {

std::string telemetryToLine(const mini_pcie_telemetry& telemetry) {
    std::ostringstream output;
    output << "device=" << telemetry.device_id
           << " temp_millic=" << telemetry.gpu_core_temp_millic
           << " power_mw=" << telemetry.gpu_power_milliwatt
           << " link_width=" << telemetry.link_width
           << " link_speed=" << telemetry.link_speed
           << " state=" << telemetry.link_state
           << " health=" << telemetry.health;
    return output.str();
}

} // namespace pcie
