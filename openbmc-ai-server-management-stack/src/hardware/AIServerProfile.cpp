#include "hardware/AIServerProfile.hpp"

#include <fstream>
#include <stdexcept>

namespace
{

/*
 * 讀取必要欄位 (required key)。
 *
 * nlohmann::json::at() 在欄位不存在時也會丟例外，但這裡先檢查 contains()，
 * 可以輸出較清楚的錯誤訊息，方便定位是哪個設定欄位缺失。
 */
template <typename T>
T requiredValue(const nlohmann::json& json, const char* key)
{
    if (!json.contains(key))
    {
        throw std::runtime_error(std::string("Missing required key: ") + key);
    }

    return json.at(key).get<T>();
}

/*
 * 檢查硬體清單不可為空。
 *
 * 本專案的管理邏輯假設 GPU、Fan、PSU、NVMe、CPU 至少各有一筆資料；
 * 若設定檔給空陣列，後續策略判斷會失去基準。
 */
void validateNonEmpty(const std::string& name, std::size_t count)
{
    if (count == 0)
    {
        throw std::runtime_error(name + " must not be empty");
    }
}

} // 匿名命名空間

namespace openbmc::hardware
{

AIServerProfile AIServerProfile::loadFromFile(const std::string& path)
{
    std::ifstream input(path);
    if (!input.is_open())
    {
        throw std::runtime_error("Unable to open profile: " + path);
    }

    nlohmann::json json;
    input >> json;
    return fromJson(json);
}

AIServerProfile AIServerProfile::fromJson(const nlohmann::json& json)
{
    AIServerProfile profile;
    profile.systemPowerBudgetWatts = requiredValue<int>(json, "system_power_budget_watts");

    if (profile.systemPowerBudgetWatts <= 0)
    {
        throw std::runtime_error("system_power_budget_watts must be positive");
    }

    for (const auto& gpuJson : requiredValue<nlohmann::json>(json, "gpus"))
    {
        common::GpuDevice gpu;
        gpu.id = requiredValue<std::string>(gpuJson, "id");
        gpu.temperatureCelsius = requiredValue<double>(gpuJson, "temperature_celsius");
        gpu.powerWatts = requiredValue<double>(gpuJson, "power_watts");
        gpu.throttled = requiredValue<bool>(gpuJson, "throttled");
        gpu.health = requiredValue<std::string>(gpuJson, "health");
        profile.gpus.push_back(gpu);
    }

    for (const auto& fanJson : requiredValue<nlohmann::json>(json, "fans"))
    {
        common::FanDevice fan;
        fan.id = requiredValue<std::string>(fanJson, "id");
        fan.rpm = requiredValue<int>(fanJson, "rpm");
        fan.pwmPercent = requiredValue<int>(fanJson, "pwm_percent");
        fan.failed = requiredValue<bool>(fanJson, "failed");
        profile.fans.push_back(fan);
    }

    for (const auto& psuJson : requiredValue<nlohmann::json>(json, "psus"))
    {
        common::PsuDevice psu;
        psu.id = requiredValue<std::string>(psuJson, "id");
        psu.outputWatts = requiredValue<double>(psuJson, "output_watts");
        psu.healthy = requiredValue<bool>(psuJson, "healthy");
        profile.psus.push_back(psu);
    }

    for (const auto& nvmeJson : requiredValue<nlohmann::json>(json, "nvmes"))
    {
        common::NvmeDevice nvme;
        nvme.id = requiredValue<std::string>(nvmeJson, "id");
        nvme.temperatureCelsius = requiredValue<double>(nvmeJson, "temperature_celsius");
        nvme.health = requiredValue<std::string>(nvmeJson, "health");
        profile.nvmes.push_back(nvme);
    }

    for (const auto& cpuJson : requiredValue<nlohmann::json>(json, "cpus"))
    {
        common::CpuDevice cpu;
        cpu.id = requiredValue<std::string>(cpuJson, "id");
        cpu.model = requiredValue<std::string>(cpuJson, "model");
        cpu.healthy = requiredValue<bool>(cpuJson, "healthy");
        profile.cpus.push_back(cpu);
    }

    validateNonEmpty("gpus", profile.gpus.size());
    validateNonEmpty("fans", profile.fans.size());
    validateNonEmpty("psus", profile.psus.size());
    validateNonEmpty("nvmes", profile.nvmes.size());
    validateNonEmpty("cpus", profile.cpus.size());

    return profile;
}

} // 命名空間 openbmc::hardware
