#pragma once

#include <string>

namespace openbmc::common
{

/*
 * 產生 UTC 時間戳 (Coordinated Universal Time timestamp)。
 *
 * 用途:
 *   EventLogger 新增事件時使用，讓 EventLog API 回傳的時間格式一致。
 *
 * 輸出:
 *   ISO 8601 風格字串，例如 2026-06-03T12:34:56Z。
 */
std::string makeUtcTimestamp();

} // 命名空間 openbmc::common
