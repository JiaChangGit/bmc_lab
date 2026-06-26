# 功耗策略說明

## 1. 這份策略在算什麼

`PowerManager` 每次收到新的 snapshot 後，會計算目前平台總功耗，並判斷是否超過 `system_power_budget_watts`。

重要名詞：

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| 功耗 | Power Consumption | 元件運作時消耗的電力 | GPU、風扇、NVMe 會被加總成 `TotalPower` |
| 功耗預算 | Power Budget | 系統允許的功耗上限 | `system_power_budget_watts` 由設定檔提供 |
| 功耗限制 | Power Capping | 超過預算時限制元件狀態 | 超標後 `HardwareModel` 會標記 GPU throttled |
| 降頻 | Throttling | 為了散熱或功耗限制降低效能 | API 中以 `Throttled` 欄位呈現 |
| PSU | Power Supply Unit | 供應伺服器電力的電源供應器 | `TotalPsuPowerWatts` 顯示健康 PSU 分攤後的輸出 |
| 鎖存 | Latch | 記住某狀態已處理過，避免重複觸發 | `budgetExceededLatched_` 避免每輪重複寫事件 |

## 2. 輸入資料

`PowerManager` 會使用以下資料：

- GPU 功耗
- 風扇 PWM 對應的估算功耗
- NVMe 的估算功耗
- 系統功率預算 `system_power_budget_watts`

另外，PSU 輸出也會被計算，但用途不同，下面會說明。CPU 目前不加進 `PowerManager` 的 `TotalPower`，但會進入 `HardwareModel::refreshPsuOutputLocked()` 的 PSU 輸出分攤。

## 3. 目前程式中的公式

### 3.1 總功耗

```text
TotalPower = TotalGpuPower + TotalFanPower + TotalNvmePower
```

### 3.2 超標判斷

```text
BudgetExceeded = TotalPower > system_power_budget_watts
```

### 3.3 PSU 為什麼不加進 `TotalPower`

`TotalPsuPower` 是 PSU 分攤後的輸出結果，不能再當成平台額外消耗加回去。

如果把它再加進去，就會變成：

- 先算一次元件負載
- 再把同一份負載用 PSU 輸出形式又算一次

這會造成重複計算。

## 4. 風扇與 NVMe 功耗怎麼估

### 4.1 風扇功耗

風扇目前用下列估算公式：

```text
2.0 + fan.pwmPercent * 0.12
```

如果風扇已失效，該顆風扇功耗視為 `0`。

### 4.2 NVMe 功耗

NVMe 目前用下列估算規則：

- `health == Critical` -> `16.0W`
- 其他情況 -> `12.0W`

## 5. 超過預算時會發生什麼事

當 `TotalPower > system_power_budget_watts`：

- `BudgetExceeded = true`
- `PowerCapActive = true`
- 所有 GPU 會進入降頻
- 若是第一次超標，會記錄 `POWER_CAP_TRIGGERED`

## 6. 為什麼事件不會每輪都重複寫

`PowerManager` 使用 `budgetExceededLatched_` 記住上一輪是否已經超標。

因此：

- 第一次超標：寫事件
- 持續超標：不重複寫事件
- 回到正常：解除鎖存
- 再次超標：重新寫事件

## 7. PSU 輸出怎麼算

`HardwareModel::refreshPsuOutputLocked()` 會：

1. 算出 GPU、風扇、NVMe、CPU 的總負載
2. 找出目前健康的 PSU 數量
3. 把負載平均分攤給每顆健康 PSU

所以：

- 若某顆 PSU 故障，該顆輸出變成 `0`
- 其他健康 PSU 會分攤剩餘負載
- 因為 PSU 分攤包含 CPU 基準負載，當 CPU 基準負載大於 `0` 時，`TotalPsuPower` 會高於 `TotalPower`

## 8. 實際例子

### 範例一：正常狀態

假設：

- `TotalGpuPower = 1800W`
- `TotalFanPower = 80W`
- `TotalNvmePower = 192W`
- `system_power_budget_watts = 2200W`

那麼：

```text
TotalPower = 1800 + 80 + 192 = 2072W
BudgetExceeded = false
```

### 範例二：全 GPU 過溫後

假設：

- `TotalGpuPower = 2500W`
- `TotalFanPower = 96W`
- `TotalNvmePower = 192W`
- `system_power_budget_watts = 2200W`

那麼：

```text
TotalPower = 2500 + 96 + 192 = 2788W
BudgetExceeded = true
```

結果會是：

- `PowerCapActive = true`
- 全部 GPU 降頻
- 事件記錄新增 `POWER_CAP_TRIGGERED`

## 9. 這份策略目前沒有做的事

目前程式沒有做：

- 機櫃層級功耗預算
- PSU 備援模式切換
- 每張 GPU 個別功耗上限
- 更精細的實際電源效率模型
- CPU 不納入 `PowerManager` 的 `TotalPower`，但會納入 PSU 輸出分攤；這是目前實作邊界，不應把兩個欄位直接相加
