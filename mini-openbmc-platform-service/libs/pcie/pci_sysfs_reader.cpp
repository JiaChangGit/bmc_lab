#include "libs/pcie/pci_sysfs_reader.hpp"

#include "libs/common/file_utils.hpp"

#include <charconv>
#include <string_view>

namespace pcie {
namespace {

std::optional<std::string> optionalText(const std::filesystem::path& path) {
    auto value = common::readTextFile(path);
    return value.ok() ? std::optional<std::string>(value.value()) : std::nullopt;
}

std::optional<int> optionalInteger(const std::filesystem::path& path) {
    auto text = optionalText(path);
    if (!text) {
        return std::nullopt;
    }
    int result{};
    const auto [ptr, error] =
        std::from_chars(text->data(), text->data() + text->size(), result);
    if (error != std::errc{} || ptr != text->data() + text->size()) {
        return std::nullopt;
    }
    return result;
}

bool isHexDigit(char value) {
    return (value >= '0' && value <= '9') ||
           (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

bool isPciBdf(std::string_view value) {
    if (value.size() != 12 || value[4] != ':' || value[7] != ':' ||
        value[10] != '.') {
        return false;
    }
    for (const auto index : {0U, 1U, 2U, 3U, 5U, 6U, 8U, 9U}) {
        if (!isHexDigit(value[index])) return false;
    }
    return value[11] >= '0' && value[11] <= '7';
}

} // namespace

PciSysfsReader::PciSysfsReader(std::filesystem::path root) : root_(std::move(root)) {}

common::StatusOr<std::vector<PciDeviceInfo>> PciSysfsReader::scan() const {
    std::error_code error;
    if (!std::filesystem::is_directory(root_, error)) {
        return common::Status::error(common::StatusCode::notFound,
                                     "PCI sysfs root is unavailable: " + root_.string());
    }
    std::vector<PciDeviceInfo> devices;
    for (const auto& entry : std::filesystem::directory_iterator(root_, error)) {
        if (error) {
            return common::Status::error(common::StatusCode::ioError,
                                         "failed while scanning PCI sysfs");
        }
        const auto bdf = entry.path().filename().string();
        if (!isPciBdf(bdf)) continue;
        PciDeviceInfo info;
        info.bdf = bdf;
        info.vendor = optionalText(entry.path() / "vendor");
        info.device = optionalText(entry.path() / "device");
        info.classCode = optionalText(entry.path() / "class");
        info.revision = optionalText(entry.path() / "revision");
        info.currentLinkSpeed = optionalText(entry.path() / "current_link_speed");
        info.currentLinkWidth = optionalInteger(entry.path() / "current_link_width");
        info.maxLinkSpeed = optionalText(entry.path() / "max_link_speed");
        info.maxLinkWidth = optionalInteger(entry.path() / "max_link_width");
        info.numaNode = optionalInteger(entry.path() / "numa_node");
        const auto driverLink = entry.path() / "driver";
        if (std::filesystem::is_symlink(driverLink, error)) {
            const auto target = std::filesystem::read_symlink(driverLink, error);
            if (!error) {
                info.driver = target.filename().string();
            }
        }
        error.clear();
        devices.push_back(std::move(info));
    }
    return devices;
}

} // namespace pcie
