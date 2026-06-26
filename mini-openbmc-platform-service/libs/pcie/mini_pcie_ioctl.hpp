#pragma once

#include "kernel/mini_pcie_telemetry/mini_pcie_telemetry.h"

#include <string>

namespace pcie {

static_assert(sizeof(mini_pcie_telemetry) <= 256,
              "ioctl telemetry structure must remain compact");
std::string telemetryToLine(const mini_pcie_telemetry& telemetry);

} // namespace pcie
