#pragma once

#include "common/ModelTypes.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace openbmc::hardware
{

class AIServerProfile
{
  public:
    int systemPowerBudgetWatts {0};
    std::vector<common::GpuDevice> gpus;
    std::vector<common::FanDevice> fans;
    std::vector<common::PsuDevice> psus;
    std::vector<common::NvmeDevice> nvmes;
    std::vector<common::CpuDevice> cpus;

    /*
     * 用途:
     *   從 JSON 設定檔載入平台初始設定。
     *
     * 輸入:
     *   path - 設定檔路徑。
     *
     * 輸出:
     *   成功回傳 AIServerProfile。
     *
     * 錯誤處理:
     *   檔案無法開啟、JSON 格式錯誤或必要欄位缺失時會丟出例外。
     *
     * 注意事項:
     *   目前只檢查必要欄位、功耗預算與非空硬體清單，沒有檢查所有溫度或 PWM 上下限。
     */
    static AIServerProfile loadFromFile(const std::string& path);

    /*
     * 用途:
     *   將已解析的 JSON 物件轉成平台設定。
     *
     * 輸入:
     *   json - nlohmann::json 物件。
     *
     * 輸出:
     *   成功回傳 AIServerProfile。
     *
     * 錯誤處理:
     *   必要欄位缺失、功耗預算小於等於 0 或硬體清單為空時會丟出例外。
     */
    static AIServerProfile fromJson(const nlohmann::json& json);
};

} // 命名空間 openbmc::hardware
