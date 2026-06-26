#pragma once

#include "libs/common/status.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace dbus {

class DbusServer {
  public:
    struct Impl;
    using FaultHandler = std::function<common::Status(
        const std::string&, const std::string&, bool)>;

    DbusServer();
    ~DbusServer();
    DbusServer(const DbusServer&) = delete;
    DbusServer& operator=(const DbusServer&) = delete;

    common::Status start();
    void stop();
    common::Status process(std::chrono::milliseconds timeout);
    common::Status upsertObject(const std::string& path,
                                const nlohmann::json& properties);
    void setFaultHandler(FaultHandler handler);

  private:
    std::unique_ptr<Impl> impl_;
};

} // namespace dbus
