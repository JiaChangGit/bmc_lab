#pragma once

#include "common/ModelTypes.hpp"

#include <systemd/sd-bus.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace openbmc::services
{
class ManagementService;
}

namespace openbmc::dbus
{

class DbusBridge
{
  public:
    /*
     * 將服務層狀態映射成 D-Bus 物件、屬性與 signal。
     * 這裡模擬 OpenBMC 守護行程 (daemon) 對本機服務發布介面 (interfaces) 的方式。
     */
    explicit DbusBridge(services::ManagementService& managementService);
    ~DbusBridge();

    /*
     * 用途:
     *   連線 D-Bus、註冊物件，並啟動背景 process loop。
     *
     * 錯誤處理:
     *   system bus 失敗時會回退 user bus；兩者都失敗時會丟出例外。
     */
    void start();

    /*
     * 用途:
     *   停止 D-Bus 背景處理並釋放 slot / bus。
     */
    void stop();

    /*
     * 用途:
     *   發出 EventGenerated signal。
     *
     * 輸入:
     *   eventRecord - EventLogger 新增的事件。
     *
     * 注意事項:
     *   若 D-Bus 尚未啟動或已停止，此函式會直接返回。
     */
    void emitEventGenerated(const common::EventRecord& eventRecord);

    /*
     * 用途:
     *   通知 D-Bus 觀察者屬性已改變。
     */
    void emitAllPropertiesChanged();
    void emitServerPropertiesChanged();

    /*
     * 用途:
     *   回傳目前使用的匯流排模式。
     *
     * 輸出:
     *   "system"、"user" 或 "disconnected"。
     */
    std::string busMode() const;

    static int serverPropertyGetter(
        sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int powerPropertyGetter(
        sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int eventPropertyGetter(
        sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);
    static int sensorPropertyGetter(
        sd_bus*, const char*, const char*, const char*, sd_bus_message*, void*, sd_bus_error*);

  private:
    enum class ObjectKind
    {
        Server,
        Power,
        Events,
        GpuSensor,
        FanSensor,
    };

    struct ObjectContext
    {
        DbusBridge* bridge {nullptr};
        ObjectKind kind {ObjectKind::Server};
        std::size_t index {0};
        std::string path;
    };

    services::ManagementService& managementService_;
    sd_bus* bus_ {nullptr};
    std::atomic<bool> running_ {false};
    std::string busMode_ {"disconnected"};
    std::thread workerThread_;
    mutable std::mutex busMutex_;
    std::vector<std::unique_ptr<ObjectContext>> contexts_;
    std::vector<sd_bus_slot*> slots_;

    void connectBus();
    void registerObjects();
    void processLoop();
    void registerObject(
        const std::string& path, const char* interfaceName, const sd_bus_vtable* vtable, ObjectKind kind,
        std::size_t index);
};

} // 命名空間 openbmc::dbus
