#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace {

enum class Flow { all, sensor, redfish, mctp, pldm, threshold };

std::optional<Flow> parseFlow(const std::string& value) {
    if (value == "all") return Flow::all;
    if (value == "sensor") return Flow::sensor;
    if (value == "redfish") return Flow::redfish;
    if (value == "mctp") return Flow::mctp;
    if (value == "pldm") return Flow::pldm;
    if (value == "threshold") return Flow::threshold;
    return std::nullopt;
}

bool matchesFlow(const nlohmann::json& record, Flow flow) {
    if (flow == Flow::all) return true;
    const auto component = record.value("component", "");
    const auto service = record.value("service", "");
    const auto message = record.value("message", "");
    switch (flow) {
    case Flow::sensor:
        return component == "bmc-sensor-service" &&
               message.find("Sensor reading") != std::string::npos;
    case Flow::redfish:
        return component == "redfish-service";
    case Flow::mctp:
        return message.find("MCTP") != std::string::npos;
    case Flow::pldm:
        return component == "pldm-endpoint-agent" ||
               record.contains("pldmType");
    case Flow::threshold:
        return service == "ThresholdEventEngine";
    case Flow::all:
        return true;
    }
    return false;
}

std::string singleLine(std::string value) {
    for (auto& character : value) {
        if (character == '\n' || character == '\r') character = ' ';
    }
    return value;
}

} // namespace

int main(int argc, char** argv) {
    Flow flow = Flow::all;
    std::filesystem::path input =
        "runtime/logs/mini-openbmc-service.jsonl";
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--flow") {
            if (index + 1 >= argc) {
                std::cerr << "--flow requires a value\n";
                return 1;
            }
            auto parsed = parseFlow(argv[++index]);
            if (!parsed) {
                std::cerr << "Unknown flow. Use all, sensor, redfish, mctp, "
                             "pldm or threshold.\n";
                return 1;
            }
            flow = *parsed;
        } else {
            input = argument;
        }
    }
    std::ifstream stream(input);
    if (!stream) {
        std::cerr << "Structured log is unavailable: " << input << '\n';
        return 1;
    }
    std::vector<nlohmann::json> records;
    std::string line;
    while (std::getline(stream, line)) {
        try {
            records.push_back(nlohmann::json::parse(line));
        } catch (const nlohmann::json::exception&) {
            std::cerr << "Skipping malformed JSONL record\n";
        }
    }
    for (const auto& record : records) {
        if (!matchesFlow(record, flow)) continue;
        std::cout << record.value("timestamp", "") << " ["
                  << record.value("component", "") << "] "
                  << record.value("message", "") << '\n';
    }
    std::filesystem::create_directories("runtime");
    std::ofstream output("runtime/generated_trace.md");
    output << "# Generated runtime trace\n\n```mermaid\nsequenceDiagram\n"
              "    participant Client\n    participant Redfish\n"
              "    participant DBus\n    participant Sensor\n"
              "    participant PLDM\n";
    for (const auto& record : records) {
        if (!matchesFlow(record, flow)) continue;
        const auto component = record.value("component", "");
        const auto message =
            singleLine(record.value("message", "operation"));
        if (component == "redfish-service") {
            output << "    Client->>Redfish: " << message << '\n';
            output << "    Redfish->>DBus: Forward operation\n";
        } else if (component == "bmc-sensor-service") {
            if (message.find("MCTP") != std::string::npos) {
                output << "    Sensor->>PLDM: " << message << '\n';
            } else {
                output << "    Sensor->>DBus: " << message << '\n';
            }
        } else if (component == "pldm-endpoint-agent") {
            output << "    PLDM-->>Sensor: " << message << '\n';
        }
    }
    output << "```\n";
    std::cout << "Mermaid trace written to runtime/generated_trace.md\n";
    return 0;
}
