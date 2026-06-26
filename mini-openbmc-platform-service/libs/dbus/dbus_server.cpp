#include "libs/dbus/dbus_server.hpp"

#include "libs/dbus/dbus_names.hpp"

#include <cerrno>
#include <cstdlib>
#include <map>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#ifdef MINI_HAS_SYSTEMD
#include <systemd/sd-bus.h>
#endif

namespace dbus {

struct DbusServer::Impl {
    struct ObjectData {
        std::string path;
        nlohmann::json properties;
#ifdef MINI_HAS_SYSTEMD
        std::vector<sd_bus_slot*> slots;
        bool exported{};
#endif
    };

    std::recursive_mutex mutex;
    std::map<std::string, std::unique_ptr<ObjectData>> objects;
    FaultHandler faultHandler;
#ifdef MINI_HAS_SYSTEMD
    sd_bus* bus{};
    sd_bus_slot* managerSlot{};
    std::set<std::string> pendingAdds;
    std::set<std::string> pendingChanges;
#endif
};

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

int stringProperty(sd_bus*, const char*, const char*, const char* property,
                   sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const auto* object =
        static_cast<const DbusServer::Impl::ObjectData*>(userdata);
    const auto iterator = object->properties.find(property);
    const std::string value =
        iterator != object->properties.end() && iterator->is_string()
            ? iterator->get<std::string>()
            : std::string{};
    return sd_bus_message_append(reply, "s", value.c_str());
}

int doubleProperty(sd_bus*, const char*, const char*, const char* property,
                   sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const auto* object =
        static_cast<const DbusServer::Impl::ObjectData*>(userdata);
    const auto iterator = object->properties.find(property);
    const double value =
        iterator != object->properties.end() && iterator->is_number()
            ? iterator->get<double>()
            : 0.0;
    return sd_bus_message_append(reply, "d", value);
}

int boolProperty(sd_bus*, const char*, const char*, const char* property,
                 sd_bus_message* reply, void* userdata, sd_bus_error*) {
    const auto* object =
        static_cast<const DbusServer::Impl::ObjectData*>(userdata);
    const auto iterator = object->properties.find(property);
    const int value =
        iterator != object->properties.end() && iterator->is_boolean() &&
        iterator->get<bool>();
    return sd_bus_message_append(reply, "b", value);
}

int listObjects(sd_bus_message* message, void* userdata, sd_bus_error*) {
    auto* impl = static_cast<DbusServer::Impl*>(userdata);
    sd_bus_message* reply{};
    int result = sd_bus_message_new_method_return(message, &reply);
    if (result < 0) return result;
    result = sd_bus_message_open_container(reply, 'a', "{ss}");
    if (result < 0) goto finish;
    {
        std::lock_guard lock(impl->mutex);
        for (const auto& [path, object] : impl->objects) {
            result = sd_bus_message_open_container(reply, 'e', "ss");
            if (result < 0) goto finish;
            const auto json = object->properties.dump();
            result = sd_bus_message_append(reply, "ss", path.c_str(), json.c_str());
            if (result < 0) goto finish;
            result = sd_bus_message_close_container(reply);
            if (result < 0) goto finish;
        }
    }
    result = sd_bus_message_close_container(reply);
    if (result < 0) goto finish;
    result = sd_bus_send(nullptr, reply, nullptr);
finish:
    sd_bus_message_unref(reply);
    return result;
}

int getObject(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto* impl = static_cast<DbusServer::Impl*>(userdata);
    const char* path{};
    int result = sd_bus_message_read(message, "s", &path);
    if (result < 0) return result;
    std::lock_guard lock(impl->mutex);
    const auto iterator = impl->objects.find(path);
    if (iterator == impl->objects.end()) {
        return sd_bus_error_setf(error, SD_BUS_ERROR_UNKNOWN_OBJECT,
                                 "Unknown MiniBMC object: %s", path);
    }
    const auto json = iterator->second->properties.dump();
    return sd_bus_reply_method_return(message, "s", json.c_str());
}

int injectFault(sd_bus_message* message, void* userdata, sd_bus_error* error) {
    auto* impl = static_cast<DbusServer::Impl*>(userdata);
    const char* target{};
    const char* fault{};
    int enabled{};
    int result = sd_bus_message_read(message, "ssb", &target, &fault, &enabled);
    if (result < 0) return result;
    std::cerr << "D-Bus fault request: target=" << target
              << " fault=" << fault << " enabled=" << enabled << '\n';
    if (!impl->faultHandler) {
        return sd_bus_error_set_const(error, SD_BUS_ERROR_NOT_SUPPORTED,
                                      "Fault injection handler is not configured");
    }
    const auto status = impl->faultHandler(target, fault, enabled != 0);
    if (!status.ok()) {
        const char* errorName =
            status.code() == common::StatusCode::invalidArgument
                ? SD_BUS_ERROR_INVALID_ARGS
                : SD_BUS_ERROR_FAILED;
        return sd_bus_error_setf(error, errorName, "%s",
                                 status.message().c_str());
    }
    std::cerr << "D-Bus fault request accepted\n";
    return sd_bus_reply_method_return(message, "b", 1);
}

const sd_bus_vtable managerVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("ListObjects", "", "a{ss}", listObjects,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetObject", "s", "s", getObject,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("InjectFault", "ssb", "b", injectFault,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};

const sd_bus_vtable sensorVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Id", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Name", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Type", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Reading", "d", doubleProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Unit", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("State", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Health", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("SourceBackend", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("SourceBus", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("SourceAddress", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ObjectPath", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("LastUpdated", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END};

const sd_bus_vtable thresholdVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UpperCritical", "d", doubleProperty, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("LowerCritical", "d", doubleProperty, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END};

const sd_bus_vtable healthVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Health", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("HealthRollup", "s", stringProperty, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("LastError", "s", stringProperty, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_VTABLE_END};

const sd_bus_vtable inventoryVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Id", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Name", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Present", "b", boolProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("PrettyName", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("DeviceType", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

const sd_bus_vtable loggingVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Id", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Severity", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Message", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Timestamp", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("OriginOfCondition", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("SensorId", "s", stringProperty, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

int addObjectVtables(DbusServer::Impl& impl,
                     DbusServer::Impl::ObjectData& object) {
    auto add = [&](const char* interface, const sd_bus_vtable* vtable) {
        sd_bus_slot* slot{};
        const int result = sd_bus_add_object_vtable(
            impl.bus, &slot, object.path.c_str(), interface, vtable, &object);
        if (result >= 0) object.slots.push_back(slot);
        return result;
    };
    if (object.properties.value("Kind", "") == "Sensor") {
        int result = add(kSensorInterface, sensorVtable);
        if (result < 0) return result;
        result = add(kThresholdInterface, thresholdVtable);
        if (result < 0) return result;
        result = add(kHealthInterface, healthVtable);
        if (result >= 0) object.exported = true;
        return result;
    }
    if (object.properties.value("Kind", "") == "Event") {
        const int result = add(kLoggingInterface, loggingVtable);
        if (result >= 0) object.exported = true;
        return result;
    }
    int result = add(kInventoryInterface, inventoryVtable);
    if (result < 0) return result;
    result = add(kHealthInterface, healthVtable);
    if (result >= 0) object.exported = true;
    return result;
}

} // namespace
#endif

DbusServer::DbusServer() : impl_(std::make_unique<Impl>()) {}

DbusServer::~DbusServer() { stop(); }

common::Status DbusServer::start() {
#ifdef MINI_HAS_SYSTEMD
    int result = openSessionBus(&impl_->bus);
    if (result < 0) {
        return common::Status::error(common::StatusCode::unavailable,
                                     "failed to connect to the D-Bus session bus");
    }
    result = sd_bus_request_name(impl_->bus, kServiceName, 0);
    if (result < 0) {
        return common::Status::error(common::StatusCode::unavailable,
                                     "failed to acquire MiniBMC D-Bus name");
    }
    result = sd_bus_add_object_vtable(impl_->bus, &impl_->managerSlot, kManagerPath,
                                      kManagerInterface, managerVtable, impl_.get());
    if (result < 0) {
        return common::Status::error(common::StatusCode::internalError,
                                     "failed to export MiniBMC manager");
    }
    for (auto& [path, object] : impl_->objects) {
        (void)path;
        result = addObjectVtables(*impl_, *object);
        if (result < 0) {
            return common::Status::error(common::StatusCode::internalError,
                                         "failed to export D-Bus object");
        }
    }
    return common::Status::okStatus();
#else
    return common::Status::okStatus();
#endif
}

void DbusServer::stop() {
#ifdef MINI_HAS_SYSTEMD
    for (auto& [path, object] : impl_->objects) {
        (void)path;
        for (auto*& slot : object->slots) slot = sd_bus_slot_unref(slot);
        object->slots.clear();
    }
    impl_->managerSlot = sd_bus_slot_unref(impl_->managerSlot);
    impl_->bus = sd_bus_flush_close_unref(impl_->bus);
#endif
}

common::Status DbusServer::process(std::chrono::milliseconds timeout) {
#ifdef MINI_HAS_SYSTEMD
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->bus) {
            return common::Status::error(common::StatusCode::unavailable,
                                         "D-Bus server is not running");
        }
        for (const auto& path : impl_->pendingAdds) {
            const auto iterator = impl_->objects.find(path);
            if (iterator == impl_->objects.end() || iterator->second->exported)
                continue;
            const int addResult = addObjectVtables(*impl_, *iterator->second);
            if (addResult < 0) {
                return common::Status::error(common::StatusCode::internalError,
                                             "failed to export D-Bus object");
            }
        }
        impl_->pendingAdds.clear();
        for (const auto& path : impl_->pendingChanges) {
            const auto iterator = impl_->objects.find(path);
            if (iterator == impl_->objects.end() || !iterator->second->exported ||
                iterator->second->properties.value("Kind", "") != "Sensor")
                continue;
            char* changed[] = {const_cast<char*>("Reading"),
                               const_cast<char*>("State"),
                               const_cast<char*>("Health"),
                               const_cast<char*>("LastUpdated"), nullptr};
            sd_bus_emit_properties_changed_strv(
                impl_->bus, path.c_str(), kSensorInterface, changed);
            char* healthChanged[] = {const_cast<char*>("Health"),
                                     const_cast<char*>("HealthRollup"),
                                     const_cast<char*>("LastError"), nullptr};
            sd_bus_emit_properties_changed_strv(
                impl_->bus, path.c_str(), kHealthInterface, healthChanged);
        }
        impl_->pendingChanges.clear();
        int result;
        do {
            result = sd_bus_process(impl_->bus, nullptr);
        } while (result > 0);
        if (result < 0) {
            return common::Status::error(common::StatusCode::ioError,
                                         "D-Bus message processing failed");
        }
    }
    const int result = sd_bus_wait(
        impl_->bus, static_cast<std::uint64_t>(timeout.count()) * 1000U);
    if (result < 0 && result != -EINTR) {
        return common::Status::error(common::StatusCode::ioError,
                                     "D-Bus wait failed");
    }
#else
    std::this_thread::sleep_for(timeout);
#endif
    return common::Status::okStatus();
}

common::Status DbusServer::upsertObject(const std::string& path,
                                        const nlohmann::json& properties) {
    std::lock_guard lock(impl_->mutex);
    const auto iterator = impl_->objects.find(path);
    if (iterator != impl_->objects.end()) {
        iterator->second->properties = properties;
#ifdef MINI_HAS_SYSTEMD
        if (impl_->bus) impl_->pendingChanges.insert(path);
#endif
        return common::Status::okStatus();
    }
    auto object = std::make_unique<Impl::ObjectData>();
    object->path = path;
    object->properties = properties;
#ifdef MINI_HAS_SYSTEMD
    if (impl_->bus) impl_->pendingAdds.insert(path);
#endif
    impl_->objects.emplace(path, std::move(object));
    return common::Status::okStatus();
}

void DbusServer::setFaultHandler(FaultHandler handler) {
    impl_->faultHandler = std::move(handler);
}

} // namespace dbus
