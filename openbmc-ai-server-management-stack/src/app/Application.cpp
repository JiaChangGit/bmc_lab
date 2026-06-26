#include "app/Application.hpp"

#include <spdlog/spdlog.h>

namespace openbmc::app
{

Application::Application(const std::string& configPath, unsigned short httpPort) :
    profile_(hardware::AIServerProfile::loadFromFile(configPath)),
    hardwareModel_(profile_),
    thermalManager_(hardwareModel_, eventLogger_),
    powerManager_(hardwareModel_, eventLogger_),
    healthMonitor_(eventLogger_),
    firmwareUpdateManager_(eventLogger_)
{
    sensorService_ = std::make_unique<services::SensorService>(
        hardwareModel_, healthMonitor_, thermalManager_, powerManager_,
        [this]() {
            if (dbusBridge_)
            {
                dbusBridge_->emitAllPropertiesChanged();
            }
        });

    faultInjectionManager_ = std::make_unique<services::FaultInjectionManager>(
        hardwareModel_,
        [this]() {
            if (sensorService_)
            {
                sensorService_->requestImmediateCycle();
            }
        });

    managementService_ = std::make_unique<services::ManagementService>(
        hardwareModel_, eventLogger_, *faultInjectionManager_, firmwareUpdateManager_);

    dbusBridge_ = std::make_unique<dbus::DbusBridge>(*managementService_);
    redfishApiServer_ = std::make_unique<redfish::RedfishApiServer>(*managementService_, "0.0.0.0", httpPort);

    eventLogger_.setEventCallback([this](const common::EventRecord& record) {
        if (dbusBridge_)
        {
            dbusBridge_->emitEventGenerated(record);
        }
    });

    firmwareUpdateManager_.setStateChangeCallback([this]() {
        if (dbusBridge_)
        {
            dbusBridge_->emitServerPropertiesChanged();
        }
    });
}

Application::~Application()
{
    stop();
}

void Application::start()
{
    if (started_)
    {
        return;
    }

    try
    {
        dbusBridge_->start();
        sensorService_->start();
        redfishApiServer_->start();
        sensorService_->requestImmediateCycle();
        started_ = true;
        spdlog::info("Application started");
    }
    catch (...)
    {
        redfishApiServer_->stop();
        sensorService_->stop();
        dbusBridge_->stop();
        throw;
    }
}

void Application::stop()
{
    if (!started_)
    {
        return;
    }

    redfishApiServer_->stop();
    sensorService_->stop();
    dbusBridge_->stop();
    started_ = false;
    spdlog::info("Application stopped");
}

} // 命名空間 openbmc::app
