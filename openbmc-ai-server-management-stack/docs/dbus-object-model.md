# D-Bus 物件模型 (Object Model)

## 文件範圍

這份文件只說明 `src/dbus/DbusBridge.cpp` 目前實作的 D-Bus 服務、物件路徑、介面、屬性與訊號。

重要名詞：

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| D-Bus | Desktop Bus | 本機行程間通訊 (IPC) | `DbusBridge` 用它讓 `busctl` 查詢服務狀態 |
| 服務名稱 | Service Name | D-Bus 上用來找到服務的名稱 | `xyz.openbmc_project.AIServer` |
| 物件路徑 | Object Path | D-Bus 物件的位置 | 例如 `/xyz/openbmc_project/ai/server` |
| 介面 | Interface | 屬性與方法的分類名稱 | 例如 `xyz.openbmc_project.AIServer.Power` |
| 屬性 | Property | 可被讀取的狀態欄位 | 例如 `TotalPower`、`FirmwareState` |
| 訊號 | Signal | 由服務主動發出的通知 | `EventGenerated` 在事件新增時送出 |
| 系統匯流排 | System Bus | 系統層級共享的 D-Bus | 程式啟動時會先嘗試使用 |
| 使用者匯流排 | User Bus | 使用者工作階段的 D-Bus | system bus 失敗時的回退路徑 |

## 服務名稱 (Service Name)

`xyz.openbmc_project.AIServer`

## 匯流排選擇策略 (Bus Selection)

守護行程採用漸進式連線策略：

1. 先連線到 system bus。
2. 請求 `xyz.openbmc_project.AIServer`。
3. 如果失敗，再連線到 user/session bus。
4. 在回退匯流排 (fallback bus) 上請求相同名稱。

這讓一般 Ubuntu 開發環境仍可執行，即使 system bus policy 不允許一般使用者持有新的服務名稱，仍可回退到 user bus。

## 核心物件路徑 (Core Object Paths)

- `/xyz/openbmc_project/ai/server`
- `/xyz/openbmc_project/ai/sensors/gpu0`
- `/xyz/openbmc_project/ai/sensors/fan0`
- `/xyz/openbmc_project/ai/power`
- `/xyz/openbmc_project/ai/events`

實作也會依照設定的平台設定檔 (platform profile)，把其餘 GPU 與風扇感測器物件一併註冊。

目前只註冊 GPU 與風扇感測器物件。PSU、NVMe、CPU 狀態仍會出現在 HTTP API 與 power summary，但沒有各自的 D-Bus sensor object。

## 介面 (Interfaces)

### `xyz.openbmc_project.AIServer.Server`

路徑：`/xyz/openbmc_project/ai/server`

屬性 (properties)：

- `SystemPowerBudgetWatts` (`int32`)
- `PowerCapActive` (`bool`)
- `FirmwareState` (`string`)
- `BusMode` (`string`)

### `xyz.openbmc_project.AIServer.Sensor`

GPU 範例路徑：`/xyz/openbmc_project/ai/sensors/gpu0`

GPU 屬性：

- `Temperature` (`double`)
- `Power` (`double`)
- `Health` (`string`)
- `Throttled` (`bool`)

風扇範例路徑：`/xyz/openbmc_project/ai/sensors/fan0`

風扇屬性：

- `Rpm` (`int32`)
- `Pwm` (`int32`)
- `Health` (`string`)

### `xyz.openbmc_project.AIServer.Power`

路徑：`/xyz/openbmc_project/ai/power`

屬性：

- `TotalPower` (`double`)
- `TotalGpuPower` (`double`)
- `TotalFanPower` (`double`)
- `TotalPsuPower` (`double`)
- `TotalNvmePower` (`double`)
- `BudgetExceeded` (`bool`)

### `xyz.openbmc_project.AIServer.EventLog`

路徑：`/xyz/openbmc_project/ai/events`

屬性：

- `EntryCount` (`uint64`)
- `LastEventId` (`string`)

訊號 (signals)：

- `EventGenerated(string timestamp, string severity, string component, string message, string eventId)`

## `busctl` 範例

如果守護行程取得的是 system bus 名稱：

```bash
busctl list | grep xyz.openbmc_project.AIServer
busctl tree xyz.openbmc_project.AIServer
busctl introspect xyz.openbmc_project.AIServer /xyz/openbmc_project/ai/server
busctl get-property xyz.openbmc_project.AIServer /xyz/openbmc_project/ai/power xyz.openbmc_project.AIServer.Power TotalPower
```

如果守護行程回退到 user bus：

```bash
busctl --user list | grep xyz.openbmc_project.AIServer
busctl --user tree xyz.openbmc_project.AIServer
busctl --user introspect xyz.openbmc_project.AIServer /xyz/openbmc_project/ai/sensors/gpu0
busctl --user get-property xyz.openbmc_project.AIServer /xyz/openbmc_project/ai/events xyz.openbmc_project.AIServer.EventLog EntryCount
```

## 範例事件訊號流程 (Example Event Signal Flow)

1. `POST /api/fault/fan-failure/fan0`
2. `FaultInjectionManager` 將 `fan0` 標記為故障。
3. `SensorService` 立即執行一次輪詢週期。
4. `HealthMonitor` 記錄 `FAN_FAILURE`。
5. `EventLogger` 的回呼呼叫 `DbusBridge::emitEventGenerated`。
6. 觀察者 (consumers) 便可看到新的訊號與更新後的事件計數。

## 限制事項

- 沒有實作 D-Bus method call；目前以屬性讀取與 signal 為主。
- D-Bus 物件不包含 PSU、NVMe、CPU sensor object。
- `DbusBridge` 會先嘗試 system bus，再回退 user bus；因此指令要依 `BusMode` 使用 `busctl` 或 `busctl --user`。
- 這份文件沒有宣告與 OpenBMC 既有 D-Bus 介面完整相容，只描述本專案目前註冊的自訂介面。
