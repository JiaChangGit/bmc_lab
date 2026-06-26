#pragma once

#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

namespace common {

class JsonLogger {
  public:
    explicit JsonLogger(std::filesystem::path path);
    void log(std::string level, std::string component, std::string message,
             nlohmann::json fields = {});

  private:
    std::filesystem::path path_;
    std::mutex mutex_;
};

} // namespace common
