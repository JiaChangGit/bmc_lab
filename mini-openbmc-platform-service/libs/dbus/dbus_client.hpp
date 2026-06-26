#pragma once

#include "libs/common/status.hpp"

#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace dbus {

class DbusClient {
  public:
    DbusClient();
    ~DbusClient();
    DbusClient(const DbusClient&) = delete;
    DbusClient& operator=(const DbusClient&) = delete;

    common::Status connect();
    common::StatusOr<std::map<std::string, nlohmann::json>> listObjects();
    common::StatusOr<nlohmann::json> getObject(const std::string& path);
    common::Status injectFault(const std::string& target,
                               const std::string& fault, bool enabled);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dbus
