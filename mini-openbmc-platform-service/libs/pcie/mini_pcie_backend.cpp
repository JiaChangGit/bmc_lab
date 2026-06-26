#include "libs/pcie/mini_pcie_backend.hpp"

#include "libs/common/file_utils.hpp"

#include <charconv>

namespace pcie {
namespace {

template <typename T>
common::StatusOr<T> readNumber(const std::filesystem::path& path) {
    auto text = common::readTextFile(path);
    if (!text.ok()) {
        return text.status();
    }
    T value{};
    const auto [ptr, error] =
        std::from_chars(text.value().data(),
                        text.value().data() + text.value().size(), value);
    if (error != std::errc{} || ptr != text.value().data() + text.value().size()) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "invalid numeric value in " + path.string());
    }
    return value;
}

} // namespace

common::StatusOr<PcieFault> pcieFaultFromString(const std::string& value) {
    if (value == "none") return PcieFault::none;
    if (value == "link_down") return PcieFault::linkDown;
    if (value == "link_degraded") return PcieFault::linkDegraded;
    if (value == "correctable_error_spike") return PcieFault::correctableErrorSpike;
    if (value == "nonfatal_error") return PcieFault::nonfatalError;
    if (value == "telemetry_timeout") return PcieFault::telemetryTimeout;
    if (value == "over_temperature") return PcieFault::overTemperature;
    if (value == "over_power") return PcieFault::overPower;
    return common::Status::error(common::StatusCode::invalidArgument,
                                 "unknown PCIe fault: " + value);
}

std::string toString(PcieFault fault) {
    switch (fault) {
    case PcieFault::none: return "none";
    case PcieFault::linkDown: return "link_down";
    case PcieFault::linkDegraded: return "link_degraded";
    case PcieFault::correctableErrorSpike: return "correctable_error_spike";
    case PcieFault::nonfatalError: return "nonfatal_error";
    case PcieFault::telemetryTimeout: return "telemetry_timeout";
    case PcieFault::overTemperature: return "over_temperature";
    case PcieFault::overPower: return "over_power";
    }
    return "none";
}

MiniPcieBackend::MiniPcieBackend(std::filesystem::path root) : root_(std::move(root)) {}

common::StatusOr<PcieTelemetry> MiniPcieBackend::readTelemetry() const {
    const auto fault = common::readTextFile(root_ / "fault_mode");
    if (fault.ok() && fault.value() == "telemetry_timeout") {
        return common::Status::error(common::StatusCode::timeout,
                                     "PCIe telemetry timeout was injected");
    }
    auto deviceId = common::readTextFile(root_ / "device_id");
    auto width = readNumber<int>(root_ / "link_width");
    auto speed = common::readTextFile(root_ / "link_speed");
    auto state = common::readTextFile(root_ / "link_state");
    auto temp = readNumber<std::int64_t>(root_ / "gpu_core_temp_millic");
    auto power = readNumber<std::int64_t>(root_ / "gpu_power_milliwatt");
    auto correctable =
        readNumber<std::uint64_t>(root_ / "correctable_error_count");
    auto nonfatal = readNumber<std::uint64_t>(root_ / "nonfatal_error_count");
    auto health = common::readTextFile(root_ / "health");
    if (!deviceId.ok()) return deviceId.status();
    if (!width.ok()) return width.status();
    if (!speed.ok()) return speed.status();
    if (!state.ok()) return state.status();
    if (!temp.ok()) return temp.status();
    if (!power.ok()) return power.status();
    if (!correctable.ok()) return correctable.status();
    if (!nonfatal.ok()) return nonfatal.status();
    if (!health.ok()) return health.status();
    return PcieTelemetry{deviceId.value(), width.value(), speed.value(), state.value(),
                         temp.value(), power.value(), correctable.value(),
                         nonfatal.value(), health.value()};
}

common::Status MiniPcieBackend::setFault(PcieFault fault) const {
    return common::writeTextFile(root_ / "fault_mode", toString(fault));
}

} // namespace pcie
