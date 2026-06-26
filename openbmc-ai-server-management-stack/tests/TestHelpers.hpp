#pragma once

#include <nlohmann/json.hpp>

namespace openbmc::tests
{

/*
 * 建立測試用平台設定 JSON。
 *
 * 用途:
 *   單元測試 (Unit Test) 需要穩定且小型的硬體資料，避免直接依賴
 *   config/ai_server_profile.json。這樣測試只驗證目標規則，不受正式設定檔變動影響。
 */
inline nlohmann::json makeProfileJson()
{
    return {
        {"system_power_budget_watts", 1200},
        {"gpus",
         nlohmann::json::array({
             {{"id", "gpu0"}, {"temperature_celsius", 60.0}, {"power_watts", 220.0}, {"throttled", false}, {"health", "OK"}},
             {{"id", "gpu1"}, {"temperature_celsius", 62.0}, {"power_watts", 230.0}, {"throttled", false}, {"health", "OK"}},
         })},
        {"fans",
         nlohmann::json::array({
             {{"id", "fan0"}, {"rpm", 3000}, {"pwm_percent", 40}, {"failed", false}},
             {{"id", "fan1"}, {"rpm", 3000}, {"pwm_percent", 40}, {"failed", false}},
         })},
        {"psus",
         nlohmann::json::array({
             {{"id", "psu0"}, {"output_watts", 350.0}, {"healthy", true}},
             {{"id", "psu1"}, {"output_watts", 350.0}, {"healthy", true}},
         })},
        {"nvmes",
         nlohmann::json::array({
             {{"id", "nvme0"}, {"temperature_celsius", 35.0}, {"health", "OK"}},
             {{"id", "nvme1"}, {"temperature_celsius", 36.0}, {"health", "OK"}},
         })},
        {"cpus",
         nlohmann::json::array({
             {{"id", "cpu0"}, {"model", "AMD EPYC 9654"}, {"healthy", true}},
             {{"id", "cpu1"}, {"model", "AMD EPYC 9654"}, {"healthy", true}},
         })},
    };
}

} // 命名空間 openbmc::tests
