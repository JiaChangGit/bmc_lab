# 散熱策略說明

## 1. 目前程式的散熱規則

`ThermalManager` 目前使用「看最高 GPU 溫度，再決定全機風扇 PWM」的做法。

重要名詞：

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| 散熱策略 | Thermal Policy | 根據溫度決定風扇與保護動作 | `ThermalManager::evaluate()` 決定風扇 PWM 與過溫事件 |
| 脈衝寬度調變 | Pulse Width Modulation, PWM | 控制風扇轉速的方式 | 以 `40%`、`70%`、`100%` 表示風扇控制輸出 |
| 每分鐘轉速 | Revolutions Per Minute, RPM | 風扇轉速單位 | Thermal API 的 `ReadingRPM` 顯示風扇轉速 |
| 過溫 | Over Temperature | 溫度超過設定門檻 | GPU 超過 `85C` 會記錄 `GPU_OVER_TEMP` |
| 降頻 | Throttling | 為了保護系統降低效能 | GPU 超過 `90C` 或 power cap 啟用時標記 `throttled` |
| 遲滯 | Hysteresis | 避免門檻附近反覆切換的控制方式 | 目前未實作，限制事項會列出 |

### 風扇策略表

| 最高 GPU 溫度 | 風扇 PWM |
| --- | --- |
| `< 70C` | `40%` |
| `70C 到 85C` | `70%` |
| `> 85C` | `100%` |

這個規則實作在 `src/services/ThermalManager.cpp`。

## 2. 過溫時會發生什麼事

### 條件一：超過 `85C`

如果任一張 GPU 溫度超過 `85C`：

- `ThermalManager` 會記錄 `GPU_OVER_TEMP`
- 風扇 PWM 會提高到 `100%`

### 條件二：超過 `90C`

如果任一張 GPU 溫度超過 `90C`：

- `HardwareModel` 會把該 GPU 標記為 `throttled`
- 事件嚴重度會變成 `Critical`

## 3. 降頻與 `ThermalManager` 的責任邊界

程式目前把責任拆成兩段：

- `ThermalManager`：決定要不要提高風扇、要不要記錄過溫事件
- `HardwareModel`：根據溫度與功耗上限，真正決定 `gpu.throttled`

這樣設計的原因是：

- 散熱規則與硬體狀態保存分開
- 其他元件讀取 snapshot，就能知道目前是否降頻

## 4. 事件為什麼不會每秒一直重複出現

`ThermalManager` 內有一個 `overTempLatched_` 集合，用來記住哪些 GPU 已經記錄過過溫事件。

這代表：

- 同一張 GPU 持續過溫時，不會每秒都新增一筆 `GPU_OVER_TEMP`
- 只有當溫度回到安全範圍後，再次升高，才會重新記錄

## 5. 實際例子

### 範例一：單張 GPU 升到 `87C`

結果會是：

- 風扇 PWM 提高到 `100%`
- 事件記錄新增 `GPU_OVER_TEMP`
- 但未必降頻，因為還沒超過 `90C`

### 範例二：單張 GPU 升到 `95C`

結果會是：

- 風扇 PWM `100%`
- 事件嚴重度為 `Critical`
- 該 GPU `throttled = true`

## 6. 和功耗策略的關係

GPU 降頻除了溫度，也會受到功耗策略影響。

`HardwareModel::refreshGpuThrottleStateLocked()` 的條件是：

```text
powerCapActive_ || gpu.temperatureCelsius > 90.0
```

所以 GPU 降頻的來源有兩種：

1. 過溫
2. 功耗超標

## 7. 目前程式沒有做的事

這份實作目前沒有做：

- 每顆風扇各自獨立控制
- 溫度遲滯區間 (hysteresis)
- 風扇分區控制 (zone-based fan control)
- 每張 GPU 對應特定風扇區域的控制表

也就是說，現在採用「整機共用一個 PWM 策略」，更細緻的硬體級風扇控制留待後續擴充。
