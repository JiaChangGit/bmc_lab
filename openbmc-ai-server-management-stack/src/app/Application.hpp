#pragma once

#include "dbus/DbusBridge.hpp"
#include "hardware/AIServerProfile.hpp"
#include "hardware/HardwareModel.hpp"
#include "redfish/RedfishApiServer.hpp"
#include "services/EventLogger.hpp"
#include "services/FaultInjectionManager.hpp"
#include "services/FirmwareUpdateManager.hpp"
#include "services/HealthMonitor.hpp"
#include "services/ManagementService.hpp"
#include "services/PowerManager.hpp"
#include "services/SensorService.hpp"
#include "services/ThermalManager.hpp"

#include <memory>
#include <string>

namespace openbmc::app
{

class Application
{
  public:
    /*
     * 集中管理程式生命週期 (lifecycle) 與依賴注入 (dependency injection)。
     * main 函式只處理參數與訊號，實際服務組裝與啟停順序都放在這裡。
     */
    Application(const std::string& configPath, unsigned short httpPort = 8080);
    ~Application();

    /*
     * 用途:
     *   依序啟動 D-Bus、SensorService 與 HTTP API。
     *
     * 錯誤處理:
     *   任一服務啟動失敗時由例外往上拋給 main() 記錄。
     *
     * 注意事項:
     *   啟動完成後會要求 SensorService 立即跑一輪，讓初始資料可被 API 查到。
     */
    void start();

    /*
     * 用途:
     *   依反向順序停止 HTTP API、SensorService 與 D-Bus。
     *
     * 注意事項:
     *   可重複呼叫；若尚未啟動，函式會直接返回。
     */
    void stop();

  private:
    hardware::AIServerProfile profile_;
    hardware::HardwareModel hardwareModel_;
    services::EventLogger eventLogger_;
    services::ThermalManager thermalManager_;
    services::PowerManager powerManager_;
    services::HealthMonitor healthMonitor_;
    services::FirmwareUpdateManager firmwareUpdateManager_;
    std::unique_ptr<services::SensorService> sensorService_;
    std::unique_ptr<services::FaultInjectionManager> faultInjectionManager_;
    std::unique_ptr<services::ManagementService> managementService_;
    std::unique_ptr<dbus::DbusBridge> dbusBridge_;
    std::unique_ptr<redfish::RedfishApiServer> redfishApiServer_;
    bool started_ {false};
};

} // 命名空間 openbmc::app
