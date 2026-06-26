#include "libs/common/logger.hpp"

#include "libs/common/time_utils.hpp"

#include <fstream>
#include <iostream>

namespace common {

JsonLogger::JsonLogger(std::filesystem::path path) : path_(std::move(path)) {
    std::filesystem::create_directories(path_.parent_path());
}

void JsonLogger::log(std::string level, std::string component, std::string message,
                     nlohmann::json fields) {
    nlohmann::json record = std::move(fields);
    record["timestamp"] = iso8601Now();
    record["component"] = std::move(component);
    record["level"] = std::move(level);
    record["message"] = std::move(message);
    std::lock_guard lock(mutex_);
    std::ofstream output(path_, std::ios::app);
    if (!output) {
        std::cerr << "Unable to open structured log: " << path_ << '\n';
        return;
    }
    output << record.dump() << '\n';
}

} // namespace common
