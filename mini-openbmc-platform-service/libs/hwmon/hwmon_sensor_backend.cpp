#include "libs/hwmon/hwmon_sensor_backend.hpp"

#include "libs/common/file_utils.hpp"

#include <charconv>
#include <set>

namespace hwmon {
namespace {

common::StatusOr<std::int64_t> readNumber(const std::filesystem::path& path) {
    auto text = common::readTextFile(path);
    if (!text.ok()) return text.status();
    std::int64_t value{};
    const auto [ptr, error] =
        std::from_chars(text.value().data(),
                        text.value().data() + text.value().size(), value);
    if (error != std::errc{} || ptr != text.value().data() + text.value().size()) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "invalid hwmon reading in " + path.string());
    }
    return value;
}

} // namespace

HwmonSensorBackend::HwmonSensorBackend(std::filesystem::path root)
    : root_(std::move(root)) {}

common::StatusOr<std::filesystem::path>
HwmonSensorBackend::discover(const std::filesystem::path& root) {
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        auto name = common::readTextFile(entry.path() / "name");
        if (name.ok() && name.value() == "mini_i2c_hwmon") {
            return entry.path();
        }
    }
    return common::Status::error(common::StatusCode::notFound,
                                 "mini_i2c_hwmon was not found");
}

common::StatusOr<HwmonReadings> HwmonSensorBackend::read() const {
    auto fault = common::readTextFile(root_ / "fault_mode");
    if (fault.ok()) {
        if (fault.value() == "read_timeout") {
            return common::Status::error(common::StatusCode::timeout,
                                         "hwmon read timeout was injected");
        }
        if (fault.value() == "device_disappeared") {
            return common::Status::error(
                common::StatusCode::unavailable,
                "hwmon provider disappearance was injected");
        }
        if (fault.value() == "invalid_reading") {
            return common::Status::error(common::StatusCode::malformedData,
                                         "invalid hwmon reading was injected");
        }
    }
    auto temp = readNumber(root_ / "temp1_input");
    auto voltage = readNumber(root_ / "in1_input");
    auto fan = readNumber(root_ / "fan1_input");
    auto tempLabel = common::readTextFile(root_ / "temp1_label");
    auto voltageLabel = common::readTextFile(root_ / "in1_label");
    auto fanLabel = common::readTextFile(root_ / "fan1_label");
    if (!temp.ok()) return temp.status();
    if (!voltage.ok()) return voltage.status();
    if (!fan.ok()) return fan.status();
    if (!tempLabel.ok()) return tempLabel.status();
    if (!voltageLabel.ok()) return voltageLabel.status();
    if (!fanLabel.ok()) return fanLabel.status();
    if (temp.value() < -100000 || temp.value() > 250000 ||
        voltage.value() < 0 || voltage.value() > 100000 ||
        fan.value() < 0 || fan.value() > 1000000) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "hwmon reading is outside validation limits");
    }
    return HwmonReadings{temp.value(), voltage.value(), fan.value(),
                         tempLabel.value(), voltageLabel.value(), fanLabel.value()};
}

common::Status HwmonSensorBackend::setFault(const std::string& fault) const {
    static const std::set<std::string> supported{
        "none", "read_timeout", "device_disappeared",
        "stuck_value", "out_of_range", "invalid_reading"};
    if (!supported.contains(fault)) {
        return common::Status::error(common::StatusCode::invalidArgument,
                                     "unknown hwmon fault: " + fault);
    }
    return common::writeTextFile(root_ / "fault_mode", fault);
}

} // namespace hwmon
