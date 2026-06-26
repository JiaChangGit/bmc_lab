# Redfish API 說明

## 1. 這份 API 文件在看什麼

這份文件只說明目前程式中實作的 HTTP 路由、回傳欄位、狀態碼與使用時機。所有內容都以 `src/redfish/RedfishApiServer.cpp` 的實作為準。

這裡的 Redfish 是 Redfish 風格 API (Redfish-style API)。Redfish API（由 DMTF 制定的硬體管理 HTTP/JSON 標準）常用來管理伺服器硬體。本專案只採用相似的路徑與 JSON 資源形狀，根節點回應也標示 `RedfishVersion` 為 `Schema-inspired`，因此不宣告完整 Redfish 標準相容。

重要名詞：

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| HTTP 方法 | HTTP Method | 表示請求目的 | `GET` 查詢狀態，`POST` 觸發動作 |
| JSON | JavaScript Object Notation | API 常用資料交換格式 | `nlohmann::json` 用來組回應 |
| 端點 | Endpoint | 可被呼叫的 API 路徑 | 例如 `/redfish/v1/UpdateService` |
| 狀態碼 | Status Code | 表示 HTTP 請求結果 | `200`、`202`、`400`、`404`、`409` |
| 請求本文 | Request Body | `POST` 時送出的內容 | `SimpleUpdate` 讀取 `image_uri` |
| OEM 欄位 | Original Equipment Manufacturer Field | 標準資源外的廠商自訂欄位 | `Oem.AIServer` 放專案自訂狀態 |

## 2. 基本資訊

- 基底 URL (Base URL)：`http://127.0.0.1:8080`
- 回應格式：JSON
- 主要方法：`GET` 與 `POST`
- 認證與加密：目前未實作登入、授權或 HTTPS

## 3. 類似 API 與選擇依據

### Redfish 風格 API vs IPMI

IPMI (Intelligent Platform Management Interface，智慧平台管理介面) 是較早期的硬體管理介面，常搭配 `ipmitool` 使用。Redfish 使用 HTTP 與 JSON，可用 `curl`、`jq` 等一般網路工具檢查回應內容。

| 比較項目 | Redfish 風格 API | IPMI |
| --- | --- | --- |
| 資料格式 | JSON | 二進位命令 |
| 常用工具 | `curl`、`jq` | `ipmitool` |
| 本專案狀態 | 已實作 HTTP/JSON 路由 | 未實作 |

本專案選擇 Redfish 風格 API，原因是 Demo 腳本可直接用 `curl` 觀察欄位，也可對照 `RedfishApiServer.cpp` 的 JSON 組裝。

### REST API vs RPC

REST API (Representational State Transfer API，表述性狀態轉移介面) 用路徑與 HTTP 方法表達資源；RPC (Remote Procedure Call，遠端程式呼叫) 的語意接近直接呼叫遠端函式。

| 比較項目 | REST API | RPC |
| --- | --- | --- |
| 表達方式 | `GET /resource`、`POST /action` | `CallMethod(args)` |
| 本專案用途 | 查詢硬體狀態與觸發少量動作 | 未使用 RPC 框架 |

目前多數端點是查詢狀態，因此使用 `GET`；韌體更新與故障注入會改變狀態，因此使用 `POST`。

### Polling vs Event

Polling (輪詢) 是固定時間查詢；Event (事件) 是狀態改變時通知。HTTP API 適合輪詢狀態，D-Bus signal 適合本機事件通知。

本專案的做法：

- HTTP `GET` 用來輪詢目前狀態。
- `EventLogger` 保存事件，讓 HTTP 可以回查。
- D-Bus `EventGenerated` 用來通知本機觀察者。

### HTTP vs HTTPS

HTTP (Hypertext Transfer Protocol，超文字傳輸協定) 沒有加密；HTTPS 會加上 TLS。專案目前只實作 HTTP，適合本機 Demo 與開發驗證。若要放到真實網路環境，需要補 TLS、認證與授權。

## 4. 路由總表

| 方法 | 路徑 | 用途 |
| --- | --- | --- |
| `GET` | `/redfish/v1` | 服務根節點 |
| `GET` | `/redfish/v1/Systems/system` | 系統摘要 |
| `GET` | `/redfish/v1/Chassis/chassis` | 機箱摘要 |
| `GET` | `/redfish/v1/Chassis/chassis/Thermal` | 散熱資料 |
| `GET` | `/redfish/v1/Chassis/chassis/Power` | 功耗資料 |
| `GET` | `/redfish/v1/Managers/bmc` | BMC 摘要 |
| `GET` | `/redfish/v1/Managers/bmc/LogServices/EventLog/Entries` | 事件記錄 |
| `GET` | `/redfish/v1/UpdateService` | 韌體更新狀態 |
| `POST` | `/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate` | 啟動韌體更新 |
| `POST` | `/api/fault/gpu-overtemp/{gpu}` | 注入 GPU 過溫 |
| `POST` | `/api/fault/fan-failure/{fan}` | 注入風扇故障 |
| `POST` | `/api/fault/psu-failure/{psu}` | 注入 PSU 故障 |
| `POST` | `/api/fault/nvme-fault/{nvme}` | 注入 NVMe 故障 |
| `POST` | `/api/fault/clear` | 清除所有故障 |

## 5. 讀取型 API

### 5.1 `GET /redfish/v1`

用途：確認服務有沒有起來，以及有哪些主資源。

回傳重點：

- `Systems`
- `Chassis`
- `Managers`
- `UpdateService`

這條 API 可作為服務入口檢查；若它正常回應，表示 HTTP 服務、基本路由與 JSON 回應都已經就緒。

### 5.2 `GET /redfish/v1/Systems/system`

用途：看整體系統摘要。

主要欄位：

- `ProcessorSummary.Count`
- `ProcessorSummary.Model`
- `Oem.AIServer.GpuCount`
- `Oem.AIServer.NvmeCount`
- `Oem.AIServer.SystemPowerBudgetWatts`
- `Oem.AIServer.PowerCapActive`

適用情境：

- 想確認平台規模
- 想知道功耗上限是否啟用
- 想看 CPU 摘要

### 5.3 `GET /redfish/v1/Chassis/chassis`

用途：提供機箱層級的入口。

這條本身不提供大量感測數值，主要是把人導向：

- `Thermal`
- `Power`

### 5.4 `GET /redfish/v1/Chassis/chassis/Thermal`

用途：看散熱相關資料。

主要欄位：

- `Temperatures[]`
- `Temperatures[].ReadingCelsius`
- `Temperatures[].Oem.AIServer.PowerWatts`
- `Temperatures[].Oem.AIServer.Throttled`
- `Fans[]`
- `Fans[].ReadingRPM`
- `Fans[].Oem.AIServer.PwmPercent`

這條 API 實際對應到：

- `HardwareModel` 內的 GPU、風扇狀態
- `ThermalManager` 設定過的風扇 PWM

### 5.5 `GET /redfish/v1/Chassis/chassis/Power`

用途：看功耗彙總與 PSU 輸出。

主要欄位：

- `PowerControl[0].PowerConsumedWatts`
- `PowerControl[0].PowerLimit.LimitInWatts`
- `PowerControl[0].Oem.AIServer.TotalGpuPowerWatts`
- `PowerControl[0].Oem.AIServer.TotalFanPowerWatts`
- `PowerControl[0].Oem.AIServer.TotalPsuPowerWatts`
- `PowerControl[0].Oem.AIServer.TotalNvmePowerWatts`
- `PowerControl[0].Oem.AIServer.BudgetExceeded`
- `PowerSupplies[]`

這條 API 的重點：

- `PowerConsumedWatts` 不會把 `TotalPsuPowerWatts` 再加進去
- `PowerConsumedWatts` 是 GPU、風扇、NVMe 的負載估算總和
- `TotalPsuPowerWatts` 是 PSU 輸出結果，目前包含 CPU 基準負載分攤

### 5.6 `GET /redfish/v1/Managers/bmc`

用途：看 BMC 摘要。

主要欄位：

- `ManagerType`
- `FirmwareVersion`
- `LogServices`
- `Oem.AIServer.FirmwareState`
- `Oem.AIServer.LastFirmwareResult`

### 5.7 `GET /redfish/v1/Managers/bmc/LogServices/EventLog/Entries`

用途：查看事件記錄。

每筆事件包含：

- `Created`
- `Severity`
- `Message`
- `SensorNumber`
- `Name`

資料來源是 `EventLogger` 保存的事件列表。

### 5.8 `GET /redfish/v1/UpdateService`

用途：查看韌體更新狀態。

主要欄位：

- `Status`
- `Oem.AIServer.FirmwareState`
- `Oem.AIServer.ImageUri`
- `Oem.AIServer.LastResult`
- `Actions.#UpdateService.SimpleUpdate.target`

## 6. 動作型 API

### 6.1 `POST /redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate`

用途：啟動韌體更新流程。

請求本文：

```json
{
  "image_uri": "file:///tmp/fw-demo-good.bin"
}
```

成功時：

- 狀態碼：`202 Accepted`
- 回應欄位：
  - `ImageUri`
  - `Message`
  - `State`

失敗時：

- `400 Bad Request`：JSON 格式錯誤
- `409 Conflict`：已有更新流程正在執行，或 `image_uri` 是空字串

呼叫流程：

```text
POST SimpleUpdate
  -> parse JSON body
  -> ManagementService::startFirmwareUpdate()
  -> FirmwareUpdateManager::startUpdate()
  -> 背景執行緒更新 FirmwareState
```

選擇依據：

- 使用 `POST`，因為這會觸發狀態機。
- `PUT` 適合覆寫整個 UpdateService 資源，和這裡的動作語意不符。

### 6.2 `POST /api/fault/gpu-overtemp/{gpu}`

用途：把指定 GPU 設成過溫狀態。

接受兩種 ID 形式：

- `gpu0`
- `0`

如果傳 `0`，伺服器會在內部正規化成 `gpu0`。

### 6.3 `POST /api/fault/fan-failure/{fan}`

用途：把指定風扇設成故障。

回應會包含：

- `Action`
- `RequestedTarget`
- `ResolvedTarget`
- `Accepted`

### 6.4 `POST /api/fault/psu-failure/{psu}`

用途：把指定 PSU 設成失效。

影響：

- PSU 輸出功率會變成 `0`
- 首次轉為故障並完成一輪健康檢查後，事件記錄會新增 `PSU_FAILURE`

### 6.5 `POST /api/fault/nvme-fault/{nvme}`

用途：把指定 NVMe 設成異常。

影響：

- 溫度提高
- 健康狀態改變
- 首次轉為異常並完成一輪健康檢查後，事件記錄會新增 `NVME_FAULT`

### 6.6 `POST /api/fault/clear`

用途：把所有故障清回基準設定。

影響：

- GPU、風扇、PSU、NVMe 回到 `baselineProfile`
- 感測與功耗狀態恢復正常流程

## 7. 哪一條 API 該在什麼時候看

| 你想回答的問題 | 對應 API |
| --- | --- |
| 系統有幾張 GPU、功耗上限有沒有啟用？ | `/redfish/v1/Systems/system` |
| GPU 現在幾度、風扇 RPM 幾轉？ | `/redfish/v1/Chassis/chassis/Thermal` |
| 現在總功耗多少、是否超標？ | `/redfish/v1/Chassis/chassis/Power` |
| 韌體更新停在哪個狀態？ | `/redfish/v1/UpdateService` |
| 最近記錄了哪些事件？ | `/redfish/v1/Managers/bmc/LogServices/EventLog/Entries` |
| 想人工製造錯誤情境 | `/api/fault/...` |

## 8. 狀態碼說明

| 狀態碼 | 代表意思 |
| --- | --- |
| `200 OK` | 一般讀取成功 |
| `202 Accepted` | 動作已被接受；韌體更新仍需透過 `GET /redfish/v1/UpdateService` 查詢後續狀態 |
| `400 Bad Request` | 請求內容格式錯誤 |
| `404 Not Found` | 路由不存在，或指定目標 ID 找不到 |
| `409 Conflict` | 目前狀態不允許這個操作，例如更新流程已在進行中，或 `image_uri` 空字串被更新管理器拒絕 |

## 9. 實際範例

### 9.1 服務入口

```bash
curl -s http://127.0.0.1:8080/redfish/v1 | jq
```

### 9.2 看 GPU 與風扇狀態

```bash
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Thermal | jq
```

### 9.3 注入風扇故障後看事件

```bash
curl -s -X POST http://127.0.0.1:8080/api/fault/fan-failure/fan0 | jq
sleep 1
curl -s http://127.0.0.1:8080/redfish/v1/Managers/bmc/LogServices/EventLog/Entries | jq
```

### 9.4 啟動韌體更新

```bash
curl -s -X POST \
  -H "Content-Type: application/json" \
  -d '{"image_uri":"file:///tmp/fw-demo-good.bin"}' \
  http://127.0.0.1:8080/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate | jq
```

## 10. 限制事項

- 目前沒有 HTTPS、登入、授權或 token。
- 沒有實作完整 Redfish schema 與服務探索，只提供專案需要的路由。
- 沒有 IPMI 介面。
- 故障注入 API 位於 `/api/fault/...`，是開發測試入口，與標準 Redfish 資源分開。
- HTTP route 沒有獨立單元測試，需透過 Demo 腳本或手動 `curl` 驗證。
