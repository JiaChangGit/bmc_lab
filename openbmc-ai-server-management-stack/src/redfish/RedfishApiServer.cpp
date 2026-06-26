#include "redfish/RedfishApiServer.hpp"

#include "common/ModelTypes.hpp"
#include "services/ManagementService.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <utility>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

namespace
{

nlohmann::json makeStatus(const std::string& health, const std::string& state = "Enabled")
{
    nlohmann::json status;
    status["Health"] = health;
    status["State"] = state;
    return status;
}

http::response<http::string_body> makeJsonResponse(
    http::status statusCode, unsigned version, const nlohmann::json& body, bool keepAlive = false)
{
    http::response<http::string_body> response {statusCode, version};
    response.set(http::field::content_type, "application/json");
    response.keep_alive(keepAlive);
    response.body() = body.dump(2);
    response.prepare_payload();
    return response;
}

http::response<http::string_body> makeErrorResponse(
    http::status statusCode, unsigned version, const std::string& message, bool keepAlive = false)
{
    nlohmann::json body;
    body["error"] = message;
    return makeJsonResponse(statusCode, version, body, keepAlive);
}

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

std::string normalizeFaultTarget(const std::string& rawTarget, const char* prefix)
{
    if (!rawTarget.empty() &&
        std::all_of(rawTarget.begin(), rawTarget.end(), [](unsigned char character) { return std::isdigit(character) != 0; }))
    {
        return std::string(prefix) + rawTarget;
    }

    return rawTarget;
}

} // 匿名命名空間

namespace openbmc::redfish
{

RedfishApiServer::RedfishApiServer(
    services::ManagementService& managementService, const std::string& bindAddress, unsigned short port) :
    managementService_(managementService),
    bindAddress_(bindAddress),
    port_(port)
{}

RedfishApiServer::~RedfishApiServer()
{
    stop();
}

void RedfishApiServer::start()
{
    if (running_.exchange(true))
    {
        return;
    }

    ioContext_.restart();
    const auto address = boost::asio::ip::make_address(bindAddress_);
    acceptor_ = std::make_unique<tcp::acceptor>(ioContext_);
    acceptor_->open(address.is_v6() ? tcp::v6() : tcp::v4());
    acceptor_->set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_->bind(tcp::endpoint(address, port_));
    acceptor_->listen();
    // 設成 non-blocking，stop() 關閉 acceptor 後，接收執行緒才有機會離開迴圈。
    acceptor_->non_blocking(true);

    acceptThread_ = std::thread([this]() { acceptLoop(); });
    spdlog::info("Redfish API listening on {}:{}", bindAddress_, port_);
}

void RedfishApiServer::stop()
{
    if (!running_.exchange(false))
    {
        return;
    }

    beast::error_code error;
    if (acceptor_ != nullptr)
    {
        // cancel + close 會中斷 acceptLoop()，避免 stop() 卡在 join()。
        acceptor_->cancel(error);
        acceptor_->close(error);
    }
    ioContext_.stop();

    if (acceptThread_.joinable())
    {
        acceptThread_.join();
    }
}

void RedfishApiServer::acceptLoop()
{
    while (running_)
    {
        beast::error_code error;
        tcp::socket socket(ioContext_);
        acceptor_->accept(socket, error);
        if (error)
        {
            if (error == boost::asio::error::would_block || error == boost::asio::error::try_again)
            {
                // 沒有新連線時短暫休息，下一輪再檢查 running_。
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            if (error == boost::asio::error::operation_aborted || error == boost::asio::error::bad_descriptor)
            {
                // stop() 關閉 acceptor 時會走到這裡，屬於正常關閉路徑。
                break;
            }

            if (running_)
            {
                spdlog::warn("HTTP accept failed: {}", error.message());
            }
            continue;
        }

        std::thread([this, socket = std::move(socket)]() mutable { handleSession(std::move(socket)); }).detach();
    }
}

void RedfishApiServer::handleSession(tcp::socket socket)
{
    beast::flat_buffer buffer;
    beast::error_code error;
    http::request<http::string_body> request;
    http::read(socket, buffer, request, error);
    if (error)
    {
        spdlog::warn("HTTP read failed: {}", error.message());
        return;
    }

    const std::string target = std::string(request.target());
    const bool keepAlive = request.keep_alive();
    const auto platform = managementService_.getPlatformSnapshot();
    http::response<http::string_body> response {http::status::ok, request.version()};

    if (request.method() == http::verb::get && target == "/redfish/v1")
    {
        nlohmann::json body;
        body["@odata.type"] = "#ServiceRoot.v1_8_0.ServiceRoot";
        body["@odata.id"] = "/redfish/v1";
        body["Id"] = "RootService";
        body["Name"] = "OpenBMC AI Server Management Stack";
        body["RedfishVersion"] = "Schema-inspired";
        body["Systems"] = {{"@odata.id", "/redfish/v1/Systems/system"}};
        body["Chassis"] = {{"@odata.id", "/redfish/v1/Chassis/chassis"}};
        body["Managers"] = {{"@odata.id", "/redfish/v1/Managers/bmc"}};
        body["UpdateService"] = {{"@odata.id", "/redfish/v1/UpdateService"}};
        body["Oem"] = {
            {"AIServer", {{"Description", "Redfish schema-inspired API, not a compliance claim"}}},
        };
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::get && target == "/redfish/v1/Systems/system")
    {
        nlohmann::json body;
        body["@odata.type"] = "#ComputerSystem.v1_17_0.ComputerSystem";
        body["@odata.id"] = "/redfish/v1/Systems/system";
        body["Id"] = "system";
        body["Name"] = "AI Training Server";
        body["Status"] = makeStatus(platform.hardware.powerCapActive ? "Warning" : "OK");
        body["ProcessorSummary"] = {
            {"Count", platform.hardware.cpus.size()},
            {"Model", platform.hardware.cpus.empty() ? "Unknown" : platform.hardware.cpus.front().model},
            {"Status", makeStatus("OK")},
        };
        body["Oem"] = {
            {"AIServer",
             {{"GpuCount", platform.hardware.gpus.size()},
              {"NvmeCount", platform.hardware.nvmes.size()},
              {"SystemPowerBudgetWatts", platform.hardware.systemPowerBudgetWatts},
              {"PowerCapActive", platform.hardware.powerCapActive}}},
        };
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::get && target == "/redfish/v1/Chassis/chassis")
    {
        nlohmann::json body;
        body["@odata.type"] = "#Chassis.v1_25_0.Chassis";
        body["@odata.id"] = "/redfish/v1/Chassis/chassis";
        body["Id"] = "chassis";
        body["Name"] = "AI Server Chassis";
        body["Status"] = makeStatus("OK");
        body["Thermal"] = {{"@odata.id", "/redfish/v1/Chassis/chassis/Thermal"}};
        body["Power"] = {{"@odata.id", "/redfish/v1/Chassis/chassis/Power"}};
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::get && target == "/redfish/v1/Chassis/chassis/Thermal")
    {
        nlohmann::json temperatures = nlohmann::json::array();
        for (const auto& gpu : platform.hardware.gpus)
        {
            temperatures.push_back({
                {"MemberId", gpu.id},
                {"Name", gpu.id + " Temperature"},
                {"ReadingCelsius", gpu.temperatureCelsius},
                {"Status", makeStatus(gpu.health)},
                {"Oem", {{"AIServer", {{"PowerWatts", gpu.powerWatts}, {"Throttled", gpu.throttled}}}}},
            });
        }

        nlohmann::json fans = nlohmann::json::array();
        for (const auto& fan : platform.hardware.fans)
        {
            fans.push_back({
                {"MemberId", fan.id},
                {"Name", fan.id},
                {"ReadingRPM", fan.rpm},
                {"Oem", {{"AIServer", {{"PwmPercent", fan.pwmPercent}}}}},
                {"Status", makeStatus(fan.failed ? "Critical" : "OK")},
            });
        }

        nlohmann::json body;
        body["@odata.type"] = "#Thermal.v1_7_0.Thermal";
        body["@odata.id"] = "/redfish/v1/Chassis/chassis/Thermal";
        body["Id"] = "Thermal";
        body["Name"] = "Thermal";
        body["Temperatures"] = temperatures;
        body["Temperatures@odata.count"] = temperatures.size();
        body["Fans"] = fans;
        body["Fans@odata.count"] = fans.size();
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::get && target == "/redfish/v1/Chassis/chassis/Power")
    {
        nlohmann::json powerSupplies = nlohmann::json::array();
        for (const auto& psu : platform.hardware.psus)
        {
            powerSupplies.push_back({
                {"MemberId", psu.id},
                {"Name", psu.id},
                {"PowerOutputWatts", psu.outputWatts},
                {"Status", makeStatus(psu.healthy ? "OK" : "Critical")},
            });
        }

        nlohmann::json body;
        body["@odata.type"] = "#Power.v1_6_0.Power";
        body["@odata.id"] = "/redfish/v1/Chassis/chassis/Power";
        body["Id"] = "Power";
        body["Name"] = "Power";
        body["PowerControl"] = nlohmann::json::array({
            {{"MemberId", "0"},
             {"Name", "System Power Control"},
             {"PowerConsumedWatts", platform.hardware.powerTelemetry.totalSystemPowerWatts},
             {"PowerLimit", {{"LimitInWatts", platform.hardware.systemPowerBudgetWatts}}},
             {"Oem",
              {{"AIServer",
                {{"TotalGpuPowerWatts", platform.hardware.powerTelemetry.totalGpuPowerWatts},
                 {"TotalFanPowerWatts", platform.hardware.powerTelemetry.totalFanPowerWatts},
                 {"TotalPsuPowerWatts", platform.hardware.powerTelemetry.totalPsuPowerWatts},
                 {"TotalNvmePowerWatts", platform.hardware.powerTelemetry.totalNvmePowerWatts},
                 {"BudgetExceeded", platform.hardware.powerTelemetry.budgetExceeded}}}}}},
        });
        body["PowerSupplies"] = powerSupplies;
        body["PowerSupplies@odata.count"] = powerSupplies.size();
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::get && target == "/redfish/v1/Managers/bmc")
    {
        nlohmann::json body;
        body["@odata.type"] = "#Manager.v1_14_0.Manager";
        body["@odata.id"] = "/redfish/v1/Managers/bmc";
        body["Id"] = "bmc";
        body["Name"] = "AI BMC Manager";
        body["ManagerType"] = "BMC";
        body["FirmwareVersion"] = "1.0.0-simulated";
        body["Status"] = makeStatus("OK");
        body["LogServices"] = {{"@odata.id", "/redfish/v1/Managers/bmc/LogServices/EventLog/Entries"}};
        body["Oem"] = {
            {"AIServer",
             {{"FirmwareState", common::toString(platform.firmware.state)},
              {"LastFirmwareResult", platform.firmware.lastResult}}},
        };
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::get &&
             target == "/redfish/v1/Managers/bmc/LogServices/EventLog/Entries")
    {
        const auto entries = managementService_.getEventLogEntries();
        nlohmann::json members = nlohmann::json::array();
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const auto& entry = entries.at(index);
            members.push_back({
                {"@odata.id",
                 "/redfish/v1/Managers/bmc/LogServices/EventLog/Entries/" + std::to_string(index + 1)},
                {"Id", std::to_string(index + 1)},
                {"Name", entry.eventId},
                {"Created", entry.timestamp},
                {"Severity", entry.severity},
                {"Message", entry.message},
                {"SensorNumber", entry.component},
            });
        }

        nlohmann::json body;
        body["@odata.type"] = "#LogEntryCollection.LogEntryCollection";
        body["@odata.id"] = "/redfish/v1/Managers/bmc/LogServices/EventLog/Entries";
        body["Name"] = "Event Log Entries";
        body["Members"] = members;
        body["Members@odata.count"] = members.size();
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::get && target == "/redfish/v1/UpdateService")
    {
        nlohmann::json body;
        body["@odata.type"] = "#UpdateService.v1_10_0.UpdateService";
        body["@odata.id"] = "/redfish/v1/UpdateService";
        body["Id"] = "UpdateService";
        body["Name"] = "Firmware Update Service";
        body["ServiceEnabled"] = true;
        body["Status"] = makeStatus(platform.firmware.busy ? "Warning" : "OK");
        body["Oem"] = {
            {"AIServer",
             {{"FirmwareState", common::toString(platform.firmware.state)},
              {"ImageUri", platform.firmware.imageUri},
              {"LastResult", platform.firmware.lastResult}}},
        };
        body["Actions"] = {
            {"#UpdateService.SimpleUpdate",
             {{"target", "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate"}}},
        };
        response = makeJsonResponse(http::status::ok, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::post &&
             target == "/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate")
    {
        nlohmann::json requestBody;
        try
        {
            requestBody = nlohmann::json::parse(request.body());
        }
        catch (const std::exception&)
        {
            response = makeErrorResponse(http::status::bad_request, request.version(), "Invalid JSON body", keepAlive);
            goto write_response;
        }

        const std::string imageUri = requestBody.value("image_uri", "");
        std::string message;
        const bool accepted = managementService_.startFirmwareUpdate(imageUri, message);

        nlohmann::json body;
        body["ImageUri"] = imageUri;
        body["Message"] = message;
        body["State"] = accepted ? "Accepted" : "Rejected";
        response = makeJsonResponse(
            accepted ? http::status::accepted : http::status::conflict, request.version(), body, keepAlive);
    }
    else if (request.method() == http::verb::post && startsWith(target, "/api/fault/gpu-overtemp/"))
    {
        const std::string requestedTarget = target.substr(std::string("/api/fault/gpu-overtemp/").size());
        const std::string gpuId = normalizeFaultTarget(requestedTarget, "gpu");
        const bool changed = managementService_.injectGpuOverTemp(gpuId);
        response = makeJsonResponse(
            changed ? http::status::accepted : http::status::not_found, request.version(),
            {{"Action", "GpuOverTemp"}, {"RequestedTarget", requestedTarget}, {"ResolvedTarget", gpuId}, {"Accepted", changed}},
            keepAlive);
    }
    else if (request.method() == http::verb::post && startsWith(target, "/api/fault/fan-failure/"))
    {
        const std::string requestedTarget = target.substr(std::string("/api/fault/fan-failure/").size());
        const std::string fanId = normalizeFaultTarget(requestedTarget, "fan");
        const bool changed = managementService_.injectFanFailure(fanId);
        response = makeJsonResponse(
            changed ? http::status::accepted : http::status::not_found, request.version(),
            {{"Action", "FanFailure"}, {"RequestedTarget", requestedTarget}, {"ResolvedTarget", fanId}, {"Accepted", changed}},
            keepAlive);
    }
    else if (request.method() == http::verb::post && startsWith(target, "/api/fault/psu-failure/"))
    {
        const std::string requestedTarget = target.substr(std::string("/api/fault/psu-failure/").size());
        const std::string psuId = normalizeFaultTarget(requestedTarget, "psu");
        const bool changed = managementService_.injectPsuFailure(psuId);
        response = makeJsonResponse(
            changed ? http::status::accepted : http::status::not_found, request.version(),
            {{"Action", "PsuFailure"}, {"RequestedTarget", requestedTarget}, {"ResolvedTarget", psuId}, {"Accepted", changed}},
            keepAlive);
    }
    else if (request.method() == http::verb::post && startsWith(target, "/api/fault/nvme-fault/"))
    {
        const std::string requestedTarget = target.substr(std::string("/api/fault/nvme-fault/").size());
        const std::string nvmeId = normalizeFaultTarget(requestedTarget, "nvme");
        const bool changed = managementService_.injectNvmeFault(nvmeId);
        response = makeJsonResponse(
            changed ? http::status::accepted : http::status::not_found, request.version(),
            {{"Action", "NvmeFault"}, {"RequestedTarget", requestedTarget}, {"ResolvedTarget", nvmeId}, {"Accepted", changed}},
            keepAlive);
    }
    else if (request.method() == http::verb::post && target == "/api/fault/clear")
    {
        managementService_.clearFaults();
        response = makeJsonResponse(
            http::status::accepted, request.version(), {{"Action", "ClearFaults"}, {"Accepted", true}}, keepAlive);
    }
    else
    {
        response = makeErrorResponse(http::status::not_found, request.version(), "Route not found", keepAlive);
    }

write_response:
    http::write(socket, response, error);
    if (error)
    {
        spdlog::warn("HTTP write failed: {}", error.message());
    }

    socket.shutdown(tcp::socket::shutdown_send, error);
}

} // 命名空間 openbmc::redfish
