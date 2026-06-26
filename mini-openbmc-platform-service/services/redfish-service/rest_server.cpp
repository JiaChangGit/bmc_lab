#include "services/redfish-service/rest_server.hpp"

#include <iostream>
#include <nlohmann/json.hpp>

namespace service {

RestServer::RestServer()
    : sensorController_(dbusClient_), pcieController_(dbusClient_),
      eventLogController_(dbusClient_),
      logger_("runtime/logs/mini-openbmc-service.jsonl") {
    registerRoutes();
    server_.set_logger([this](const httplib::Request& request,
                              const httplib::Response& response) {
        logger_.log("INFO", "redfish-service", "Redfish request completed",
                    {{"service", "RestServer"},
                     {"objectPath", request.path},
                     {"state", request.method + " " +
                                   std::to_string(response.status)}});
    });
}

int RestServer::run(const std::string& address, int port) {
    const auto connected = dbusClient_.connect();
    if (!connected.ok()) {
        std::cerr << "Redfish service cannot connect to D-Bus: "
                  << connected.message() << '\n';
        return 1;
    }
    std::cout << "Redfish service listening on http://" << address << ':' << port
              << '\n';
    return server_.listen(address, port) ? 0 : 1;
}

void RestServer::stop() {
    bool expected = false;
    if (stopped_.compare_exchange_strong(expected, true)) server_.stop();
}

void RestServer::registerRoutes() {
    server_.Get("/redfish/v1", [](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200,
                 {{"@odata.type", "#ServiceRoot.v1_15_0.ServiceRoot"},
                  {"@odata.id", "/redfish/v1"},
                  {"Id", "RootService"},
                  {"Name", "Mini OpenBMC Redfish Service"},
                  {"Chassis", {{"@odata.id", "/redfish/v1/Chassis"}}},
                  {"Systems", {{"@odata.id", "/redfish/v1/Systems"}}},
                  {"Managers", {{"@odata.id", "/redfish/v1/Managers"}}}});
    });
    server_.Get("/redfish/v1/Chassis",
                [](const httplib::Request&, httplib::Response& res) {
                    sendJson(res, 200,
                             {{"@odata.type",
                               "#ChassisCollection.ChassisCollection"},
                              {"@odata.id", "/redfish/v1/Chassis"},
                              {"Name", "Chassis Collection"},
                              {"Members@odata.count", 1},
                              {"Members",
                               {{{"@odata.id",
                                  "/redfish/v1/Chassis/GPU0"}}}}});
                });
    server_.Get("/redfish/v1/Systems",
                [](const httplib::Request&, httplib::Response& res) {
                    sendJson(res, 200,
                             {{"@odata.type",
                               "#ComputerSystemCollection."
                               "ComputerSystemCollection"},
                              {"@odata.id", "/redfish/v1/Systems"},
                              {"Name", "Computer System Collection"},
                              {"Members@odata.count", 1},
                              {"Members",
                               {{{"@odata.id",
                                  "/redfish/v1/Systems/System0"}}}}});
                });
    server_.Get("/redfish/v1/Systems/System0",
                [](const httplib::Request&, httplib::Response& res) {
                    sendJson(
                        res, 200,
                        {{"@odata.type",
                          "#ComputerSystem.v1_22_0.ComputerSystem"},
                         {"@odata.id", "/redfish/v1/Systems/System0"},
                         {"Id", "System0"},
                         {"Name", "Mini OpenBMC Managed System"},
                         {"SystemType", "Physical"},
                         {"Status",
                          {{"State", "Enabled"}, {"Health", "OK"}}},
                         {"LogServices",
                          {{"@odata.id",
                            "/redfish/v1/Systems/System0/LogServices"}}}});
                });
    server_.Get(
        "/redfish/v1/Systems/System0/LogServices",
        [](const httplib::Request&, httplib::Response& res) {
            sendJson(
                res, 200,
                {{"@odata.type",
                  "#LogServiceCollection.LogServiceCollection"},
                 {"@odata.id",
                  "/redfish/v1/Systems/System0/LogServices"},
                 {"Name", "Log Service Collection"},
                 {"Members@odata.count", 1},
                 {"Members",
                  {{{"@odata.id",
                     "/redfish/v1/Systems/System0/LogServices/EventLog"}}}}});
        });
    server_.Get(
        "/redfish/v1/Systems/System0/LogServices/EventLog",
        [](const httplib::Request&, httplib::Response& res) {
            sendJson(
                res, 200,
                {{"@odata.type", "#LogService.v1_8_0.LogService"},
                 {"@odata.id",
                  "/redfish/v1/Systems/System0/LogServices/EventLog"},
                 {"Id", "EventLog"},
                 {"Name", "System Event Log"},
                 {"Entries",
                  {{"@odata.id",
                    "/redfish/v1/Systems/System0/LogServices/EventLog/Entries"}}}});
        });
    server_.Get("/redfish/v1/Managers",
                [](const httplib::Request&, httplib::Response& res) {
                    sendJson(res, 200,
                             {{"@odata.type",
                               "#ManagerCollection.ManagerCollection"},
                              {"@odata.id", "/redfish/v1/Managers"},
                              {"Name", "Manager Collection"},
                              {"Members@odata.count", 1},
                              {"Members",
                               {{{"@odata.id",
                                  "/redfish/v1/Managers/BMC0"}}}}});
                });
    server_.Get("/redfish/v1/Chassis/GPU0",
                [](const httplib::Request&, httplib::Response& res) {
                    sendJson(res, 200,
                             {{"@odata.type", "#Chassis.v1_25_0.Chassis"},
                              {"@odata.id", "/redfish/v1/Chassis/GPU0"},
                              {"Id", "GPU0"},
                              {"Name", "Mini GPU Chassis"},
                              {"ChassisType", "Enclosure"},
                              {"Status", {{"State", "Enabled"}, {"Health", "OK"}}},
                              {"Sensors",
                               {{"@odata.id",
                                 "/redfish/v1/Chassis/GPU0/Sensors"}}},
                              {"PCIeDevices",
                               {{"@odata.id",
                                 "/redfish/v1/Chassis/GPU0/PCIeDevices"}}}});
                });
    server_.Get("/redfish/v1/Chassis/GPU0/Sensors",
                [this](const httplib::Request&, httplib::Response& res) {
                    auto result = sensorController_.collection();
                    result.ok() ? sendJson(res, 200, result.value())
                                : sendStatusError(res, result.status());
                });
    server_.Get(R"(/redfish/v1/Chassis/GPU0/Sensors/([^/]+))",
                [this](const httplib::Request& req, httplib::Response& res) {
                    auto result = sensorController_.get(req.matches[1]);
                    result.ok() ? sendJson(res, 200, result.value())
                                : sendStatusError(res, result.status());
                });
    server_.Get("/redfish/v1/Chassis/GPU0/PCIeDevices",
                [this](const httplib::Request&, httplib::Response& res) {
                    auto result = pcieController_.collection();
                    result.ok() ? sendJson(res, 200, result.value())
                                : sendStatusError(res, result.status());
                });
    server_.Get(R"(/redfish/v1/Chassis/GPU0/PCIeDevices/([^/]+))",
                [this](const httplib::Request& req, httplib::Response& res) {
                    auto result = pcieController_.get(req.matches[1]);
                    result.ok() ? sendJson(res, 200, result.value())
                                : sendStatusError(res, result.status());
                });
    server_.Get(
        "/redfish/v1/Systems/System0/LogServices/EventLog/Entries",
        [this](const httplib::Request&, httplib::Response& res) {
            auto result = eventLogController_.collection();
            result.ok() ? sendJson(res, 200, result.value())
                        : sendStatusError(res, result.status());
        });
    server_.Get("/redfish/v1/Managers/BMC0",
                [](const httplib::Request&, httplib::Response& res) {
                    sendJson(res, 200,
                             {{"@odata.type", "#Manager.v1_18_0.Manager"},
                              {"@odata.id", "/redfish/v1/Managers/BMC0"},
                              {"Id", "BMC0"},
                              {"Name", "Mini OpenBMC Manager"},
                              {"ManagerType", "BMC"},
                              {"FirmwareVersion", "1.0.0"},
                              {"Status", {{"State", "Enabled"}, {"Health", "OK"}}}});
                });
    server_.Get("/redfish/v1/Managers/BMC0/Health",
                [this](const httplib::Request&, httplib::Response& res) {
                    auto objects = dbusClient_.listObjects();
                    if (!objects.ok()) {
                        sendStatusError(res, objects.status());
                        return;
                    }
                    std::string health = "OK";
                    for (const auto& [path, properties] : objects.value()) {
                        (void)path;
                        if (properties.value("Health", "OK") == "Critical") {
                            health = "Critical";
                            break;
                        }
                        if (properties.value("Health", "OK") == "Warning") {
                            health = "Warning";
                        }
                    }
                    sendJson(res, 200,
                             {{"Id", "BMC0Health"},
                              {"Name", "MiniBMC Aggregate Health"},
                              {"Status", {{"State", "Enabled"}, {"Health", health}}}});
                });
    server_.Post("/debug/faults",
                 [this](const httplib::Request& req, httplib::Response& res) {
                     try {
                         const auto body = nlohmann::json::parse(req.body);
                         if (!body.contains("target") || !body.contains("fault") ||
                             !body.contains("enabled")) {
                             sendJson(res, 400,
                                      {{"error", "target, fault and enabled are required"}});
                             return;
                         }
                         const auto status = dbusClient_.injectFault(
                             body.at("target").get<std::string>(),
                             body.at("fault").get<std::string>(),
                             body.at("enabled").get<bool>());
                         if (!status.ok()) {
                             sendStatusError(res, status);
                             return;
                         }
                         logger_.log("INFO", "redfish-service",
                                     "Fault request forwarded through D-Bus",
                                     {{"service", "RestServer"},
                                      {"sensorId", body.at("target")},
                                      {"state", body.at("fault")}});
                         sendJson(res, 200,
                                  {{"accepted", true},
                                   {"target", body.at("target")},
                                   {"fault", body.at("fault")},
                                   {"enabled", body.at("enabled")}});
                     } catch (const nlohmann::json::exception& error) {
                         sendJson(res, 400, {{"error", error.what()}});
                     }
                 });
}

void RestServer::sendJson(httplib::Response& response, int status,
                          const nlohmann::json& body) {
    response.status = status;
    response.set_content(body.dump(2), "application/json");
}

void RestServer::sendStatusError(httplib::Response& response,
                                 const common::Status& status) {
    int code = 500;
    switch (status.code()) {
    case common::StatusCode::invalidArgument:
    case common::StatusCode::malformedData:
        code = 400;
        break;
    case common::StatusCode::notFound:
        code = 404;
        break;
    case common::StatusCode::unavailable:
    case common::StatusCode::timeout:
    case common::StatusCode::ioError:
        code = 503;
        break;
    case common::StatusCode::ok:
        code = 200;
        break;
    case common::StatusCode::internalError:
        code = 500;
        break;
    }
    sendJson(response, code,
             {{"error",
               {{"code", "MiniBMC." + std::to_string(static_cast<int>(status.code()))},
                {"message", status.message()}}}});
}

} // namespace service
