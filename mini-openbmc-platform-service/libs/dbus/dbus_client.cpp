#include "libs/dbus/dbus_client.hpp"

#include "libs/dbus/dbus_names.hpp"

#include <cstdlib>

#ifdef MINI_HAS_SYSTEMD
#include <systemd/sd-bus.h>
#endif

namespace dbus {

#ifdef MINI_HAS_SYSTEMD
namespace {

int openSessionBus(sd_bus** bus) {
    const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    if (!address || address[0] == '\0') return sd_bus_open_user(bus);
    int result = sd_bus_new(bus);
    if (result < 0) return result;
    result = sd_bus_set_address(*bus, address);
    if (result < 0) {
        *bus = sd_bus_unref(*bus);
        return result;
    }
    result = sd_bus_set_bus_client(*bus, true);
    if (result < 0) {
        *bus = sd_bus_unref(*bus);
        return result;
    }
    result = sd_bus_start(*bus);
    if (result < 0) *bus = sd_bus_unref(*bus);
    return result;
}

} // namespace
#endif

struct DbusClient::Impl {
#ifdef MINI_HAS_SYSTEMD
    sd_bus* bus{};
#endif
};

DbusClient::DbusClient() : impl_(std::make_unique<Impl>()) {}

DbusClient::~DbusClient() {
#ifdef MINI_HAS_SYSTEMD
    impl_->bus = sd_bus_flush_close_unref(impl_->bus);
#endif
}

common::Status DbusClient::connect() {
#ifdef MINI_HAS_SYSTEMD
    if (impl_->bus) return common::Status::okStatus();
    const int result = openSessionBus(&impl_->bus);
    if (result < 0) {
        return common::Status::error(common::StatusCode::unavailable,
                                     "failed to connect to D-Bus session bus");
    }
    return common::Status::okStatus();
#else
    return common::Status::error(
        common::StatusCode::unavailable,
        "D-Bus support was not compiled; install libsystemd-dev and rebuild");
#endif
}

common::StatusOr<std::map<std::string, nlohmann::json>>
DbusClient::listObjects() {
#ifdef MINI_HAS_SYSTEMD
    auto connected = connect();
    if (!connected.ok()) return connected;
    sd_bus_error error{};
    sd_bus_message* reply{};
    int result = sd_bus_call_method(impl_->bus, kServiceName, kManagerPath,
                                    kManagerInterface, "ListObjects", &error,
                                    &reply, "");
    if (result < 0) {
        sd_bus_error_free(&error);
        return common::Status::error(common::StatusCode::unavailable,
                                     "MiniBMC D-Bus service is unavailable");
    }
    std::map<std::string, nlohmann::json> objects;
    result = sd_bus_message_enter_container(reply, 'a', "{ss}");
    while (result > 0) {
        result = sd_bus_message_enter_container(reply, 'e', "ss");
        if (result <= 0) break;
        const char* path{};
        const char* json{};
        result = sd_bus_message_read(reply, "ss", &path, &json);
        if (result < 0) break;
        try {
            objects.emplace(path, nlohmann::json::parse(json));
        } catch (const nlohmann::json::exception&) {
            result = -1;
            break;
        }
        result = sd_bus_message_exit_container(reply);
        if (result < 0) break;
    }
    if (result >= 0) result = sd_bus_message_exit_container(reply);
    sd_bus_message_unref(reply);
    if (result < 0) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "invalid ListObjects D-Bus reply");
    }
    return objects;
#else
    return common::Status::error(common::StatusCode::unavailable,
                                 "D-Bus support was not compiled");
#endif
}

common::StatusOr<nlohmann::json>
DbusClient::getObject(const std::string& path) {
#ifdef MINI_HAS_SYSTEMD
    auto connected = connect();
    if (!connected.ok()) return connected;
    sd_bus_error error{};
    sd_bus_message* reply{};
    int result = sd_bus_call_method(impl_->bus, kServiceName, kManagerPath,
                                    kManagerInterface, "GetObject", &error,
                                    &reply, "s", path.c_str());
    if (result < 0) {
        sd_bus_error_free(&error);
        return common::Status::error(common::StatusCode::notFound,
                                     "D-Bus object was not found");
    }
    const char* json{};
    result = sd_bus_message_read(reply, "s", &json);
    sd_bus_message_unref(reply);
    if (result < 0) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "invalid GetObject D-Bus reply");
    }
    try {
        return nlohmann::json::parse(json);
    } catch (const nlohmann::json::exception&) {
        return common::Status::error(common::StatusCode::malformedData,
                                     "D-Bus object JSON is invalid");
    }
#else
    (void)path;
    return common::Status::error(common::StatusCode::unavailable,
                                 "D-Bus support was not compiled");
#endif
}

common::Status DbusClient::injectFault(const std::string& target,
                                       const std::string& fault, bool enabled) {
#ifdef MINI_HAS_SYSTEMD
    auto connected = connect();
    if (!connected.ok()) return connected;
    sd_bus_error error{};
    sd_bus_message* reply{};
    int result = sd_bus_call_method(impl_->bus, kServiceName, kManagerPath,
                                    kManagerInterface, "InjectFault", &error,
                                    &reply, "ssb", target.c_str(), fault.c_str(),
                                    enabled ? 1 : 0);
    if (result < 0) {
        const std::string message = error.message ? error.message : "fault injection failed";
        const auto code = sd_bus_error_has_name(&error, SD_BUS_ERROR_INVALID_ARGS)
                              ? common::StatusCode::invalidArgument
                              : common::StatusCode::ioError;
        sd_bus_error_free(&error);
        return common::Status::error(code, message);
    }
    int accepted{};
    result = sd_bus_message_read(reply, "b", &accepted);
    sd_bus_message_unref(reply);
    if (result < 0 || !accepted) {
        return common::Status::error(common::StatusCode::ioError,
                                     "fault injection was rejected");
    }
    return common::Status::okStatus();
#else
    (void)target;
    (void)fault;
    (void)enabled;
    return common::Status::error(common::StatusCode::unavailable,
                                 "D-Bus support was not compiled");
#endif
}

} // namespace dbus
