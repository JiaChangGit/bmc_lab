# BMC API 與資料流說明報告

## 這份文件在說明什麼

這份報告列出 API 清單，並補充「需求背景」、「每條 API 實際對到哪一層」、「哪些端點看起來相近但用途不同」、「程式碼可確認的問題情境」。

核心資料流如下：

> 平台狀態先收斂到服務層，再由 HTTP API 與 D-Bus 各自對外呈現。

資料來源保持一致後，除錯時可以從 API 欄位追回服務層與硬體模型。

這份報告用三個問題串起來：

1. 這條 API 是讀狀態，還是改變狀態？
2. 回傳欄位來自哪一個服務或硬體模型？
3. 如果結果不對，先查 HTTP、服務層、硬體模型，還是 D-Bus？

這三個問題能協助讀者把 API 路徑、服務層與硬體模型連在一起，避免只記路徑名稱。

## 需求背景

本專案設定檔中的 AI 伺服器平台包含多張 GPU、多顆風扇、多顆 PSU 與多顆 NVMe。BMC 類型的管理程式需要持續整理這些資料，並提供固定介面讓外部工具查詢或觸發動作。

本專案的需求可以分成三類：

| 需求 | 英文 | 說明 | 程式對應 |
| --- | --- | --- | --- |
| 狀態查詢 | State Query | 查詢溫度、功耗、風扇、事件與更新狀態 | `GET /redfish/v1/...`、`ManagementService::getPlatformSnapshot()` |
| 狀態變更 | State Change | 啟動韌體更新或注入故障情境 | `POST SimpleUpdate`、`POST /api/fault/...` |
| 本機觀察 | Local Observation | 讓本機工具讀取屬性或接收事件訊號 | `DbusBridge`、`busctl` |

這裡的 Redfish 是 Redfish 風格 API (Redfish-style API)。Redfish 原本是 DMTF 制定的硬體管理標準；本專案只使用相似的路徑與 JSON 資源形狀，並在 `/redfish/v1` 回應中標示 `RedfishVersion` 為 `Schema-inspired`。因此文件會稱為「受 Redfish 結構啟發」，不宣告完整 Redfish 標準相容。

重要名詞先整理如下：

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| Redfish 風格 API | Redfish-style API | 以 HTTP/JSON 表達硬體管理資源 | `RedfishApiServer` 的 `/redfish/v1/...` 路由 |
| D-Bus | Desktop Bus | 本機行程間通訊 (IPC) | `DbusBridge` 匯出狀態屬性與事件訊號 |
| REST API | Representational State Transfer API | 用 HTTP 方法與資源路徑操作資料 | `GET` 查狀態，`POST` 觸發動作 |
| 遠端程式呼叫 | Remote Procedure Call, RPC | 以函式呼叫語意觸發遠端動作 | 本專案沒有採用 RPC 框架，動作用 HTTP `POST` 表示 |
| 輪詢 | Polling | 固定時間查詢狀態 | `SensorService` 每秒跑一輪 |
| 事件 | Event | 記錄需要保存的狀態變化 | `EventLogger` 保存事件並通知 D-Bus |
| 狀態機 | State Machine | 用明確階段表示流程 | `FirmwareUpdateManager` 表示韌體更新進度 |

## 先認識四種對外介面

系統裡有四種你最常看到的對外入口：

| 介面 | 代表範例 | 適合做什麼 | 為什麼存在 |
| --- | --- | --- | --- |
| Redfish 風格讀取 API | `GET /redfish/v1/Chassis/chassis/Power` | 看目前平台狀態 | 給 HTTP 客戶端與管理工具使用 |
| Redfish 風格動作 API | `POST /redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate` | 觸發韌體更新流程 | 用動作端點表示狀態變更 |
| 開發用故障注入 API | `POST /api/fault/gpu-overtemp/gpu0` | 人工觸發故障情境 | 降低驗證策略行為的操作成本 |
| D-Bus 物件模型 | `busctl --user get-property ... TotalPower` | 看內部服務狀態與訊號 | 模擬 OpenBMC 類型系統內部使用的資料匯流排 |

### HTTP API 與 D-Bus 差在哪裡

| 比較項目 | HTTP API | D-Bus |
| --- | --- | --- |
| 存取方式 | HTTP + JSON | 本機程式間通訊 (IPC) |
| 目標讀者 | 外部管理程式、測試腳本 | 本機服務、除錯人員 |
| 資料形狀 | 資源導向 (resource-oriented) | 物件 / 介面 / 屬性 |
| 觀察事件 | 透過事件記錄端點重新查詢 | 可直接接收訊號 (signal) |
| 使用工具 | `curl`、瀏覽器、自動化測試 | `busctl`、D-Bus client |

使用情境：

- 要看 HTTP 回應格式與外部查詢流程，使用 HTTP API。
- 要確認本機物件模型與事件訊號是否發布，使用 D-Bus。

## API 與通訊方式比較

### Redfish 風格 API vs IPMI

IPMI (Intelligent Platform Management Interface，智慧平台管理介面) 是較早期的硬體管理介面，常用於電源控制、感測器查詢與事件紀錄。Redfish 使用 HTTP 與 JSON，可用 `curl`、`jq` 等一般網路工具檢查回應內容。

| 比較項目 | Redfish 風格 API | IPMI |
| --- | --- | --- |
| 資料格式 | JSON | 二進位命令與回應 |
| 常用工具 | `curl`、瀏覽器、自動化測試 | `ipmitool` |
| 可讀性 | 路徑與欄位較直觀 | 需要理解 command / netfn / sensor number |
| 本專案採用情況 | 已實作 HTTP/JSON 路由 | 未實作 |

本專案選擇 Redfish 風格 API 的原因：

- 程式已使用 `nlohmann::json` 組回應，欄位可對照原始碼。
- Demo 腳本可以用 `curl` 與 `jq` 直接觀察結果。
- 讀取型與動作型 API 可用 HTTP method 區分。

限制是：目前只實作專案需要的路由，完整 Redfish schema 與 IPMI command 相容層不在範圍內。

### REST API vs RPC

REST API (Representational State Transfer API，表述性狀態轉移介面) 強調資源路徑與 HTTP 方法；RPC (Remote Procedure Call，遠端程式呼叫) 的語意接近在網路上呼叫函式。

| 比較項目 | REST API | RPC |
| --- | --- | --- |
| 表達方式 | `GET /resource`、`POST /action` | `CallFunction(args)` |
| 適合情境 | 查詢硬體資源、呈現狀態 | 內部服務呼叫或複雜命令 |
| 本專案採用情況 | HTTP 路由採 REST 風格 | 未使用 RPC 框架 |

本專案選擇 REST 風格，是因為目前 API 多數是查詢平台狀態；少數動作，例如 `SimpleUpdate`，也能放在明確的 action endpoint。

### Polling vs Event

Polling (輪詢) 是固定時間檢查狀態；Event (事件) 是狀態改變時發出通知。

| 比較項目 | Polling | Event |
| --- | --- | --- |
| 特性 | 固定時間執行，流程可重現 | 狀態改變時通知觀察者 |
| 需要處理的成本 | 需要等待下一輪，查詢次數由呼叫端控制 | 需要處理訂閱、訊號與重送 |
| 本專案採用情況 | `SensorService` 每秒輪詢 | `EventLogger` 與 D-Bus `EventGenerated` |

本專案兩者都用：感測與策略判斷靠輪詢，重要狀態變化再記成事件。故障注入後會呼叫 `SensorService.requestImmediateCycle()`，避免一定要等滿一秒才看到結果。

### HTTP vs HTTPS

HTTP (Hypertext Transfer Protocol，超文字傳輸協定) 是未加密的請求/回應協定；HTTPS 是在 HTTP 外層加上 TLS 加密與憑證驗證。

| 比較項目 | HTTP | HTTPS |
| --- | --- | --- |
| 加密 | 無 | 有 TLS |
| 本機 Demo | 設定簡單 | 需要憑證與 TLS 設定 |
| 本專案採用情況 | 已實作 | 未實作 |

本專案使用 HTTP，原因是程式重點在資料流、策略與 API 形狀。若要放到真實網路環境，必須補上 HTTPS、認證與授權。

## 系統架構與模組職責

```text
curl / busctl / demo script
          |
          v
+-----------------------------+
| RedfishApiServer / DbusBridge|
+---------------+-------------+
                |
                v
        ManagementService
                |
  +-------------+-----------------------------+
  |             |                             |
  v             v                             v
SensorService  FaultInjectionManager  FirmwareUpdateManager
  |
  v
HealthMonitor -> ThermalManager -> PowerManager
  |
  v
EventLogger
  |
  v
HardwareModel
```

| 模組 | 職責 | 範圍外 |
| --- | --- | --- |
| `RedfishApiServer` | 接 HTTP、解析路徑、組 JSON 回應 | 不直接保存硬體狀態 |
| `DbusBridge` | 匯出 D-Bus 物件、屬性與事件訊號 | 不決定散熱或功耗策略 |
| `ManagementService` | 統一查詢 snapshot、事件、故障注入與韌體更新入口 | 不做複雜策略計算 |
| `HardwareModel` | 保存平台可變狀態與故障注入結果 | 不處理 HTTP 或 D-Bus |
| `SensorService` | 週期性推進感測、健康、散熱與功耗流程 | 不組 API 回應 |
| `HealthMonitor` | 判斷風扇、PSU、NVMe 健康事件 | 不修改硬體基準設定 |
| `ThermalManager` | 根據最高 GPU 溫度調整風扇 PWM 並記錄過溫事件 | 不直接處理 HTTP 請求 |
| `PowerManager` | 計算功耗、判斷是否超過預算、啟動 power cap | 不把 PSU 輸出加回 `TotalPower` |
| `FirmwareUpdateManager` | 用背景執行緒模擬韌體更新狀態機 | 不下載或燒錄真實韌體 |
| `EventLogger` | 保存事件並通知回呼 | 不判斷事件是否該發生 |

### 先看 HTTP 方法，再看路徑

閱讀 API 時，可先看 HTTP 方法 (HTTP method)，再看路徑：

| 方法 | 英文概念 | 在本專案的用法 |
| --- | --- | --- |
| `GET` | Read / Query | 查詢目前狀態，不改變系統 |
| `POST` | Action / Command | 觸發一個動作，例如韌體更新或故障注入 |
| `PUT` | Replace | 本專案沒有使用，因為沒有提供整份資源覆寫 |
| `PATCH` | Partial Update | 本專案沒有使用，因為目前沒有局部修改設定需求 |

例如 `GET /redfish/v1/UpdateService` 是查狀態；`POST /redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate` 是觸發更新。兩者都和韌體更新有關，但語意不同。

## 一個請求如何進入程式

下面這張圖是最重要的主線：

```text
HTTP 請求
  |
  v
RedfishApiServer
  |
  v
ManagementService
  |
  +--> FaultInjectionManager --------+
  |                                  |
  +--> FirmwareUpdateManager         |
  |                                  v
  +--> 讀取 PlatformSnapshot   HardwareModel
                                     |
                                     v
                               SensorService 週期更新
                                     |
                                     v
                       Health / Thermal / Power 管理器
                                     |
                                     v
                                EventLogger
                                     |
                        +------------+------------+
                        |                         |
                        v                         v
                  DbusBridge                Redfish 查詢端點
```

### 這條路徑的重點

- `RedfishApiServer` 只負責接 HTTP、解析路徑、組 JSON。
- `ManagementService` 是進入服務層的唯一主要入口。
- 真正的狀態保存在 `HardwareModel`，HTTP 層只負責組回應。
- 事件先寫進 `EventLogger`，之後 D-Bus 與 Redfish 才能觀察到一致的內容。

這條路徑也說明一個設計選擇：HTTP 層不要直接修改事件記錄。以故障注入為例，API 只負責要求 `FaultInjectionManager` 改變硬體模型，之後由感測輪詢與各管理器判斷是否要記事件。這樣可保留「硬體狀態改變後，再由管理器判斷事件」的資料路徑。

## API 總表

### 讀取型 API

| 方法 | 路徑 | 用途 | 對應資料 |
| --- | --- | --- | --- |
| `GET` | `/redfish/v1` | 服務入口，列出主要資源 | 固定服務描述 |
| `GET` | `/redfish/v1/Systems/system` | 系統摘要 | CPU 數量、GPU/NVMe 數量、功耗上限狀態 |
| `GET` | `/redfish/v1/Chassis/chassis` | 機箱層入口 | 指向 Thermal 與 Power |
| `GET` | `/redfish/v1/Chassis/chassis/Thermal` | 散熱資料 | GPU 溫度、GPU 功耗、風扇 RPM、PWM |
| `GET` | `/redfish/v1/Chassis/chassis/Power` | 電力資料 | 總功耗、元件功耗、PSU 輸出 |
| `GET` | `/redfish/v1/Managers/bmc` | BMC 摘要 | 韌體狀態摘要與事件記錄入口 |
| `GET` | `/redfish/v1/Managers/bmc/LogServices/EventLog/Entries` | 事件記錄 | `EventLogger` 內的事件 |
| `GET` | `/redfish/v1/UpdateService` | 韌體更新狀態 | 更新狀態機目前狀態 |

### 動作型 API

| 方法 | 路徑 | 用途 | 成功狀態碼 |
| --- | --- | --- | --- |
| `POST` | `/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate` | 啟動韌體更新流程 | `202 Accepted` |
| `POST` | `/api/fault/gpu-overtemp/{gpu}` | 觸發 GPU 過溫 | `202 Accepted` |
| `POST` | `/api/fault/fan-failure/{fan}` | 觸發風扇故障 | `202 Accepted` |
| `POST` | `/api/fault/psu-failure/{psu}` | 觸發 PSU 故障 | `202 Accepted` |
| `POST` | `/api/fault/nvme-fault/{nvme}` | 觸發 NVMe 故障 | `202 Accepted` |
| `POST` | `/api/fault/clear` | 清除所有故障 | `202 Accepted` |

### 錯誤狀態碼

| 狀態碼 | 何時出現 |
| --- | --- |
| `400 Bad Request` | `SimpleUpdate` 的 JSON 本文格式錯誤 |
| `404 Not Found` | 路由不存在，或故障注入目標 ID 找不到 |
| `409 Conflict` | 韌體更新已在進行中，又再次送 `SimpleUpdate` |

### 狀態碼怎麼判讀

`202 Accepted` 表示「請求已被接受」。韌體更新就是這種情況：`POST SimpleUpdate` 成功後，只代表工作流程已啟動，後續仍要用 `GET /redfish/v1/UpdateService` 查 `FirmwareState`。

`409 Conflict` 則代表請求本身格式正確，但現在狀態不允許執行。例如韌體更新已在進行中，又送第二次更新。這和 `400 Bad Request` 不同，`400` 是請求格式或內容本身有問題。

常用判斷方式：

| 你看到的結果 | 先檢查什麼 |
| --- | --- |
| `400` | JSON 欄位名稱、內容型別、是否有 `image_uri` |
| `404` | API 路徑、故障注入目標是否存在，例如 `gpu0`、`fan0` |
| `409` | 目前是否已有背景工作執行中 |
| `202` 但狀態沒變 | 是否有等待輪詢週期，或背景工作是否仍在進行 |

## 看起來很像，但其實用途不同的 API

### `GET /Systems/system` 與 `GET /Chassis/chassis/Power`

這兩條都會提到功耗，層級不同。

| 路徑 | 適合回答的問題 |
| --- | --- |
| `/Systems/system` | 這台系統整體有幾張 GPU？功耗上限有沒有啟用？ |
| `/Chassis/chassis/Power` | 現在總功耗多少？PSU 各自輸出多少？功耗超標了嗎？ |

選擇依據很簡單：

- 想看摘要，用 `Systems/system`
- 想看數值細節，用 `Chassis/chassis/Power`

實際除錯時，`/Systems/system` 可以先確認平台規模與 `PowerCapActive`。如果 `PowerCapActive` 是 `true`，再查 `/Chassis/chassis/Power` 看是哪一類元件造成總功耗上升。

### `GET /UpdateService` 與 `POST ...SimpleUpdate`

| 路徑 | 類型 | 用途 |
| --- | --- | --- |
| `/redfish/v1/UpdateService` | 讀取型 | 查目前更新狀態 |
| `/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate` | 動作型 | 真的啟動更新流程 |

這是典型的「查詢」與「改變狀態」分離：

- `GET` 不改變狀態。
- `POST` 才拿來觸發工作流程。

這種分離有助於除錯。當 `POST` 回傳成功但狀態沒有如預期變化時，可以用 `GET` 反覆確認狀態機是否卡在 `Downloading`、`Verifying`、`Installing` 或 `Rollback`。

### `/api/fault/...` 與一般 Redfish 讀取 API

故障注入 API 用來快速製造狀態變化，方便確認：

- `Thermal` 會不會變
- `Power` 會不會超標
- `EventLog` 會不會新增事件
- `DbusBridge` 會不會送出訊號

它的定位是開發測試工具，一般平台營運時不會暴露給外部。

因此路徑放在 `/api/fault/...`，與標準 Redfish 資源分開。它的用途是開發測試，正式機房會開放的管理 API 不會採用這種入口。

### API 選擇依據整理

| 你想做的事 | 對應 API | 原因 |
| --- | --- | --- |
| 確認服務是否活著 | `GET /redfish/v1` | 路由固定，回應內容不依賴硬體故障狀態 |
| 看整台機器摘要 | `GET /redfish/v1/Systems/system` | 適合看規模與高階狀態 |
| 看風扇與 GPU 溫度 | `GET /redfish/v1/Chassis/chassis/Thermal` | 散熱資訊集中在 Chassis Thermal |
| 看功耗是否超標 | `GET /redfish/v1/Chassis/chassis/Power` | 功耗拆分與預算判斷集中在 Power |
| 查韌體更新進度 | `GET /redfish/v1/UpdateService` | 讀取狀態機目前階段 |
| 啟動韌體更新 | `POST ...SimpleUpdate` | 觸發動作 |
| 製造測試故障 | `POST /api/fault/...` | 專案提供的開發測試入口 |
| 查內部物件狀態 | `busctl --user ...` | 可確認 D-Bus 物件是否同步 |

## 重要 API 詳細說明

這一段以 `src/redfish/RedfishApiServer.cpp` 的路由為準，補上輸入、輸出、錯誤情境與選擇依據。

### 服務入口：`GET /redfish/v1`

用途：

- 確認 HTTP 服務是否啟動。
- 列出主要資源入口。

輸入：

- 無 request body。

輸出：

- `Systems`
- `Chassis`
- `Managers`
- `UpdateService`
- `RedfishVersion`

錯誤情境：

- 路由拼錯時會走一般 `404 Not Found`。
- 服務未啟動時，`curl` 會連線失敗，這不會進到程式路由。

呼叫流程：

```text
GET /redfish/v1
  -> RedfishApiServer::handleRequest()
  -> buildServiceRoot()
  -> JSON response
```

選擇原因與替代方案：

- 這條是最小查詢端點，適合做健康檢查。
- 替代方式是查 D-Bus service name，但那只能確認本機 D-Bus，不能確認 HTTP 路由。

### 系統摘要：`GET /redfish/v1/Systems/system`

用途：

- 查詢平台規模與高階狀態。

輸入：

- 無 request body。

輸出：

- CPU 數量與型號。
- GPU / NVMe 數量。
- `SystemPowerBudgetWatts`
- `PowerCapActive`

錯誤情境：

- 路由不存在時回 `404`。

呼叫流程：

```text
GET /Systems/system
  -> ManagementService::getPlatformSnapshot()
  -> buildSystem()
  -> JSON response
```

選擇原因與替代方案：

- 適合先看整台機器摘要。
- 若要看功耗細節，應改查 `/redfish/v1/Chassis/chassis/Power`。

### 散熱資料：`GET /redfish/v1/Chassis/chassis/Thermal`

用途：

- 查詢 GPU 溫度、GPU 功耗、GPU 降頻狀態、風扇 RPM 與 PWM。

輸入：

- 無 request body。

輸出：

- `Temperatures[]`
- `Fans[]`

錯誤情境：

- 路由不存在時回 `404`。
- 若設定檔沒有 GPU 或風扇，程式會在設定載入階段失敗，不會進入此 API。

呼叫流程：

```text
GET /Thermal
  -> ManagementService::getPlatformSnapshot()
  -> HardwareModel::snapshot()
  -> buildThermal()
```

選擇原因與替代方案：

- Thermal 端點集中呈現散熱資料，適合觀察 `ThermalManager` 的結果。
- 替代方式是查 D-Bus GPU / Fan sensor 屬性；D-Bus 適合本機除錯，HTTP API 適合 Demo 與自動化查詢。

### 功耗資料：`GET /redfish/v1/Chassis/chassis/Power`

用途：

- 查詢總功耗、功耗預算、PSU 輸出與是否超標。

輸入：

- 無 request body。

輸出：

- `PowerControl[0].PowerConsumedWatts`
- `PowerControl[0].PowerLimit.LimitInWatts`
- `Oem.AIServer.TotalGpuPowerWatts`
- `Oem.AIServer.TotalFanPowerWatts`
- `Oem.AIServer.TotalPsuPowerWatts`
- `Oem.AIServer.TotalNvmePowerWatts`
- `Oem.AIServer.BudgetExceeded`
- `PowerSupplies[]`

錯誤情境：

- 路由不存在時回 `404`。

呼叫流程：

```text
GET /Power
  -> ManagementService::getPlatformSnapshot()
  -> buildPower()
```

選擇原因與替代方案：

- 這條最適合除錯 `PowerManager::evaluate()`。
- 若只想看是否啟動 power cap，可先看 `/Systems/system` 的 `PowerCapActive`。

### 事件記錄：`GET /redfish/v1/Managers/bmc/LogServices/EventLog/Entries`

用途：

- 查詢 `EventLogger` 目前保存的事件。

輸入：

- 無 request body。

輸出：

- `Members[]`
- `Members@odata.count`

錯誤情境：

- 路由不存在時回 `404`。
- 事件尚未發生時，`Members` 是空陣列。

呼叫流程：

```text
GET /EventLog/Entries
  -> ManagementService::getEventLogEntries()
  -> EventLogger::entries()
  -> buildEventLogEntries()
```

選擇原因與替代方案：

- HTTP 事件記錄適合回查歷史。
- D-Bus `EventGenerated` 適合即時觀察新事件，但需要本機 client 監聽訊號。

### 韌體更新狀態：`GET /redfish/v1/UpdateService`

用途：

- 查詢韌體更新狀態機目前在哪個階段。

輸入：

- 無 request body。

輸出：

- `FirmwareState`
- `ImageUri`
- `LastResult`
- `Actions.#UpdateService.SimpleUpdate.target`

錯誤情境：

- 路由不存在時回 `404`。

呼叫流程：

```text
GET /UpdateService
  -> ManagementService::getFirmwareStatus()
  -> FirmwareUpdateManager::status()
  -> buildUpdateService()
```

選擇原因與替代方案：

- 韌體更新是背景流程，`POST` 後需要用這條 `GET` 輪詢。
- 替代方式是看 Event Log，但事件只能看關鍵轉折，不能完整代表目前狀態。

### 啟動韌體更新：`POST /redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate`

用途：

- 啟動 `FirmwareUpdateManager` 的非同步更新流程。

輸入：

```json
{
  "image_uri": "file:///tmp/fw-demo-good.bin"
}
```

輸出：

- 成功：`202 Accepted`，回傳 `State=Accepted`。
- 失敗：回傳 `Message` 與 `State=Rejected`。

錯誤情境：

| 情境 | 實際狀態碼 | 原因 |
| --- | --- | --- |
| JSON 格式錯誤 | `400 Bad Request` | `nlohmann::json::parse` 失敗 |
| `image_uri` 空字串 | `409 Conflict` | `FirmwareUpdateManager::startUpdate()` 拒絕啟動 |
| 更新流程已在執行 | `409 Conflict` | 避免同時啟動兩個更新執行緒 |

呼叫流程：

```text
POST SimpleUpdate
  -> parse JSON body
  -> ManagementService::startFirmwareUpdate()
  -> FirmwareUpdateManager::startUpdate()
  -> background worker
```

選擇原因與替代方案：

- 使用 `POST` 是因為它會觸發狀態變更。
- `PUT /UpdateService` 適合整份資源覆寫，和這裡的動作語意不符。
- 不使用 RPC，是因為目前 HTTP 路由已足以表達「對 UpdateService 執行 SimpleUpdate 動作」。

### 故障注入：`POST /api/fault/...`

用途：

- 人工改變硬體模型，驗證散熱、功耗、健康檢查、事件與 API 輸出。

輸入：

- URL path target，例如 `gpu0`、`fan0`。
- 也接受純數字，例如 `0`，伺服器會正規化成對應 ID。

輸出：

- `Action`
- `RequestedTarget`
- `ResolvedTarget`
- `Accepted`

錯誤情境：

- 目標 ID 找不到時回 `404 Not Found`，`Accepted=false`。
- 路由拼錯時回 `404 Not Found`。

呼叫流程：

```text
POST /api/fault/fan-failure/fan0
  -> normalize target
  -> ManagementService::injectFanFailure()
  -> FaultInjectionManager
  -> HardwareModel
  -> SensorService.requestImmediateCycle()
```

選擇原因與替代方案：

- 這是開發測試入口，與正式 Redfish 資源分開。
- 若要模擬真實硬體錯誤，替代方式會是底層 sensor driver 或 BMC 硬體訊號；本專案沒有實作真實硬體讀取。

### 清除故障：`POST /api/fault/clear`

用途：

- 清除 GPU、風扇、PSU、NVMe 的故障注入狀態。

輸入：

- 無 request body。

輸出：

- `Action=ClearFaults`
- `Accepted=true`

錯誤情境：

- 路由拼錯時回 `404 Not Found`。

呼叫流程：

```text
POST /api/fault/clear
  -> ManagementService::clearFaults()
  -> FaultInjectionManager::clearFaults()
  -> HardwareModel::clearFaults()
  -> SensorService.requestImmediateCycle()
```

注意事項：

- `clearFaults()` 會重設故障來源。
- 功耗上限與遙測結果會在後續感測與功耗週期中重新計算。

## 各端點實際怎麼用

### 1. 服務根節點 `GET /redfish/v1`

用途：確認整個 API 有哪些主資源。

```bash
curl -s http://127.0.0.1:8080/redfish/v1 | jq
```

重點欄位：

- `Systems`
- `Chassis`
- `Managers`
- `UpdateService`

自動化測試可用這條 API 確認服務是否啟動、主要資源是否已掛上。

### 2. 系統摘要 `GET /redfish/v1/Systems/system`

用途：看整台機器的概觀；單一感測器細節由其他端點呈現。

重要欄位：

- `ProcessorSummary.Count`
- `Oem.AIServer.GpuCount`
- `Oem.AIServer.NvmeCount`
- `Oem.AIServer.SystemPowerBudgetWatts`
- `Oem.AIServer.PowerCapActive`

若只需確認：

- 這台模擬平台規模多大
- 目前是否啟動功耗限制

這條端點就足夠。

### 3. 散熱資料 `GET /redfish/v1/Chassis/chassis/Thermal`

用途：看 GPU 溫度與風扇狀態。

```bash
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Thermal | jq
```

重要欄位：

- `Temperatures[].ReadingCelsius`
- `Temperatures[].Oem.AIServer.PowerWatts`
- `Temperatures[].Oem.AIServer.Throttled`
- `Fans[].ReadingRPM`
- `Fans[].Oem.AIServer.PwmPercent`

欄位判讀方式：

| 欄位 | 判讀重點 |
| --- | --- |
| `ReadingCelsius` | GPU 溫度；超過策略門檻時會拉高風扇 |
| `PowerWatts` | 單張 GPU 的估算功耗 |
| `Throttled` | 是否因過溫或功耗限制而降頻 |
| `ReadingRPM` | 風扇轉速；故障時會變成 `0` |
| `PwmPercent` | 風扇控制輸出；數字越高代表風扇策略越積極 |

### 4. 電力資料 `GET /redfish/v1/Chassis/chassis/Power`

用途：看目前功耗預算與功耗拆分。

這條是理解 `PowerManager` 最直接的 API。

重要欄位：

- `PowerControl[0].PowerConsumedWatts`
- `PowerControl[0].PowerLimit.LimitInWatts`
- `PowerControl[0].Oem.AIServer.TotalGpuPowerWatts`
- `PowerControl[0].Oem.AIServer.TotalFanPowerWatts`
- `PowerControl[0].Oem.AIServer.TotalPsuPowerWatts`
- `PowerControl[0].Oem.AIServer.TotalNvmePowerWatts`
- `PowerControl[0].Oem.AIServer.BudgetExceeded`

### `TotalPsuPowerWatts` 與 `PowerConsumedWatts` 的差異

兩種數值要分開判讀。

- `PowerConsumedWatts` 代表 `PowerManager` 計算的 GPU、風扇、NVMe 負載總和。
- `TotalPsuPowerWatts` 代表 `HardwareModel` 依 GPU、風扇、NVMe 與 CPU 基準負載分攤後的 PSU 輸出結果。

如果把兩者再加在一起，會造成重複計算 (double count)。文件需要在此處明確區分兩個欄位。

本專案目前的 `PowerConsumedWatts` 計算方式是：

```text
PowerConsumedWatts
  = TotalGpuPowerWatts
  + TotalFanPowerWatts
  + TotalNvmePowerWatts
```

`TotalPsuPowerWatts` 是 PSU 輸出分攤後的觀察值，不再加回 `PowerConsumedWatts`。目前程式在 PSU 輸出計算中也納入 CPU 基準負載；當 CPU 基準負載大於 `0` 時，`TotalPsuPowerWatts` 會高於只加總 GPU、Fan、NVMe 的 `PowerConsumedWatts`。這是現有模型的實作差異，文件與除錯時要分開判讀。

### 5. BMC 摘要 `GET /redfish/v1/Managers/bmc`

用途：從管理器角度看韌體狀態與事件記錄入口。

重要欄位：

- `FirmwareVersion`
- `Oem.AIServer.FirmwareState`
- `Oem.AIServer.LastFirmwareResult`
- `LogServices.@odata.id`

觀察韌體更新流程時，這條與 `/UpdateService` 需要一起看。

### 6. 事件記錄 `GET /redfish/v1/Managers/bmc/LogServices/EventLog/Entries`

用途：確認故障、功耗超標、韌體更新是否真的留下事件。

每一筆事件至少有：

- `Created`
- `Severity`
- `Message`
- `SensorNumber`
- `Name`

實際例子：

```bash
curl -s http://127.0.0.1:8080/redfish/v1/Managers/bmc/LogServices/EventLog/Entries | jq
```

若已注入風扇故障，且 `HealthMonitor` 已完成一輪判斷，會看到 `FAN_FAILURE` 類型訊息。

事件記錄有兩個用途：

1. 讓 Redfish 可以查到歷史事件。
2. 讓 D-Bus 可以透過 `EventGenerated` 訊號通知本機觀察者。

因此如果 API 的事件有新增，但 D-Bus 沒有訊號，優先檢查 `DbusBridge.emitEventGenerated()`；如果兩邊都沒有，優先檢查 `HealthMonitor`、`ThermalManager` 或 `PowerManager` 的事件判斷。

### 7. 更新服務 `GET /redfish/v1/UpdateService`

用途：輪詢韌體更新狀態。

重要欄位：

- `Status`
- `Oem.AIServer.FirmwareState`
- `Oem.AIServer.ImageUri`
- `Oem.AIServer.LastResult`
- `Actions.#UpdateService.SimpleUpdate.target`

這條 API 適合搭配 `SimpleUpdate` 使用：

1. 先送 `POST`
2. 再用 `GET` 反覆查狀態變化

### 8. 啟動韌體更新 `POST /redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate`

請求本文：

```json
{
  "image_uri": "file:///tmp/fw-demo-good.bin"
}
```

成功時回傳：

```json
{
  "ImageUri": "file:///tmp/fw-demo-good.bin",
  "Message": "Firmware update workflow started",
  "State": "Accepted"
}
```

### 這條 API 的選擇依據

`PUT /UpdateService` 不適合這條 API：

- 這裡是在要求系統執行一段工作流程。
- 整個 UpdateService 資源內容維持不變。

因此用動作型端點 `Actions/...SimpleUpdate` 能直接表達「觸發更新流程」。

### 9. 故障注入 `POST /api/fault/...`

故障注入 API 有一個輸入處理規則：它接受完整 ID，也接受純數字索引。

例如這兩個都可以：

```bash
curl -s -X POST http://127.0.0.1:8080/api/fault/gpu-overtemp/gpu0 | jq
curl -s -X POST http://127.0.0.1:8080/api/fault/gpu-overtemp/0 | jq
```

程式會把 `0` 正規化成 `gpu0`。這樣設計的原因是降低腳本與人工測試的輸入成本。

回傳範例：

```json
{
  "Action": "GpuOverTemp",
  "RequestedTarget": "0",
  "ResolvedTarget": "gpu0",
  "Accepted": true
}
```

這裡的正規化 (normalization) 是一種輸入整理。API 接受比較短的寫法，但內部仍轉成一致的硬體 ID。這樣事件記錄、D-Bus 物件與 Redfish 欄位就不會同時出現 `0` 和 `gpu0` 兩種名稱。

## 四個常用流程圖

### 流程一：GPU 過溫注入到事件記錄

```text
POST /api/fault/gpu-overtemp/gpu0
        |
        v
FaultInjectionManager
        |
        v
HardwareModel.injectGpuOverTemp()
        |
        v
SensorService.requestImmediateCycle()
        |
        v
ThermalManager / PowerManager / HealthMonitor
        |
        v
EventLogger.logEvent()
        |
        +--> DbusBridge.emitEventGenerated()
        |
        +--> EventLog API 可查到新事件
```

### 流程二：韌體更新

```text
POST SimpleUpdate
      |
      v
FirmwareUpdateManager.startUpdate()
      |
      v
背景工作執行緒
  Downloading
      |
      v
  Verifying
      |
      +--> 若映像名稱含 verify-fail / bad / corrupt
      |        -> Rollback
      |
      v
  Installing
      |
      +--> 若映像名稱含 install-fail / fail-install
      |        -> Rollback
      |
      v
  RebootPending
      |
      v
  Completed
```

### 流程三：同一份資料如何同時出現在 Redfish 與 D-Bus

```text
HardwareModel / FirmwareUpdateStatus
              |
              v
       ManagementService
         /          \
        v            v
DbusBridge      RedfishApiServer
        |            |
        v            v
  busctl 查詢      curl 查詢
```

### 流程四：輪詢與事件鎖存

```text
SensorService 每秒執行
        |
        v
取得 HardwareSnapshot
        |
        +--> HealthMonitor 檢查故障
        +--> ThermalManager 檢查過溫
        +--> PowerManager 檢查功耗
        |
        v
事件是否已經記錄過？
        |
   +----+----+
   |         |
  是        否
   |         |
不重複寫    EventLogger.logEvent()
```

事件鎖存 (event latch) 的重點是避免同一個故障每秒都寫一筆事件。例如 `fan0` 故障後，只要故障狀態還沒清除，就不會每秒新增一筆 `FAN_FAILURE`。等故障清掉後，鎖存狀態會釋放；下一次再發生時才會重新記錄。

## 實際操作範例

### 範例一：看散熱資料，再注入故障

```bash
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Thermal | jq
curl -s -X POST http://127.0.0.1:8080/api/fault/fan-failure/fan0 | jq
sleep 1
curl -s http://127.0.0.1:8080/redfish/v1/Managers/bmc/LogServices/EventLog/Entries | jq
```

預期觀察到三件事：

1. `fan0` 的 RPM 變成 `0`
2. 事件記錄新增 `FAN_FAILURE`
3. 若再查 D-Bus，對應物件的 `Health` 也會改變

這個範例的重點是驗證「寫入狀態」與「觀察狀態」是分開的。`POST /api/fault/fan-failure/fan0` 只負責製造故障；後面的 `GET EventLog` 才是確認策略與事件路徑是否生效。

### 範例二：觸發功耗超標

```bash
for gpu in gpu0 gpu1 gpu2 gpu3 gpu4 gpu5 gpu6 gpu7; do
  curl -s -X POST "http://127.0.0.1:8080/api/fault/gpu-overtemp/${gpu}" > /dev/null
done
sleep 2
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Power | jq
```

重點看：

- `BudgetExceeded`
- `PowerConsumedWatts`
- `PowerLimit.LimitInWatts`

判讀方式是：如果 `PowerConsumedWatts` 大於 `PowerLimit.LimitInWatts`，`BudgetExceeded` 應為 `true`，且 `PowerManager` 會啟用 power cap，使 GPU 被標示為 `Throttled`。如果數值超標但 `BudgetExceeded` 沒變，問題要回頭查 `PowerManager::evaluate()` 是否有重新計算。

### 範例三：模擬韌體驗證失敗

```bash
curl -s -X POST \
  -H "Content-Type: application/json" \
  -d '{"image_uri":"file:///tmp/fw-verify-fail.bin"}' \
  http://127.0.0.1:8080/redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate | jq

curl -s http://127.0.0.1:8080/redfish/v1/UpdateService | jq
```

這種情境用來確認：

- 狀態是否進入 `Rollback`
- 事件記錄是否出現 `FW_VERIFY_FAILED`

這個範例故意用檔名中的 `verify-fail` 觸發驗證失敗。程式透過字串判斷規則模擬韌體更新狀態機，讓這條流程在沒有真實 BMC 韌體映像的環境中也能重現失敗路徑。

## 困難與挑戰：程式碼可確認的問題情境

下面案例都能從目前程式碼、腳本或錯誤處理邏輯確認。Git 歷史目前只有初始 commit，無法證明每個情境都曾在開發過程實際發生過，因此這裡整理為「程式碼已處理或特別防範的問題情境」。每個案例依序列出現象、原因、處理方式與驗證方式。

本段會用到的關鍵字：

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| PID 檔 | PID File | 記錄背景行程 ID，用於後續停止或檢查 | `scripts/03_run.sh` 與 `scripts/99_cleanup.sh` 使用 `run/ai-bmc-manager.pid` |
| 競態條件 | Race Condition | 多個動作的時間順序不同，導致結果不一致 | 背景啟動時，shell 建立子行程不代表服務初始化已完成 |
| 存活檢查 | Liveness Check | 確認行程是否仍存在 | 腳本用 `kill -0` 與 `pgrep` 檢查背景服務 |
| 阻塞式 I/O | Blocking I/O | 呼叫會停在等待狀態，直到事件發生 | HTTP `accept()` 若不可中斷，停止流程會卡在 `join()` |
| 非阻塞式 I/O | Non-blocking I/O | 呼叫不長時間停住，程式可定期檢查停止條件 | `RedfishApiServer` 的 acceptor 設為 non-blocking |
| 執行緒等待 | Thread Join | 主執行緒等待背景執行緒結束 | `stop()` 需要確認 accept loop 能離開 |
| D-Bus Policy | D-Bus Policy | 控制誰能在 system bus 持有服務名稱 | 一般使用者沒有 policy 時會回退 user bus |
| 回退 | Fallback | 第一個方法失敗時改用替代方法 | `DbusBridge` system bus 失敗後改用 user bus |

### 問題情境 1：背景模式重複啟動與 PID 檔可信度

#### 問題現象

若背景服務已經在執行，又再執行一次：

```bash
./scripts/03_run.sh background
```

程式會因為 D-Bus 服務名稱已被占用而啟動失敗。若腳本只記錄 `$!`，PID 檔就會指向剛失敗的新行程，和原本正在執行的服務不一致。

```text
Failed to request D-Bus service name on user bus: File exists
```

#### 發生條件

- 已有 `ai-bmc-manager` 背景行程。
- 再次執行 `./scripts/03_run.sh background`。
- 腳本若沒有先檢查既有行程或 PID 檔，就會產生錯誤判斷。

#### 根本原因

背景啟動有兩個不同階段：

1. shell 建立子行程。
2. `ai-bmc-manager` 完成 D-Bus 名稱註冊、HTTP 監聽與服務初始化。

`$!` 只代表第一階段成功，不代表第二階段也成功。如果沒有檢查既有行程與新行程存活狀態，PID 檔會變成不可信資訊。

這是一種競態條件 (race condition)：在不同執行時序下，腳本記錄 PID 的時間點會早於服務初始化成功或失敗的時間點。

#### 除錯方式

可以對照三個來源：

- `run/ai-bmc-manager.pid`
- `pgrep -f build/ai-bmc-manager`
- `run/ai-bmc-manager.log`

如果 PID 檔指向的行程不存在，但 `pgrep` 找到另一個仍在執行的服務，就代表 PID 檔不能當唯一事實來源。

#### 解決方法

目前 `scripts/03_run.sh` 做了三層保護：

1. 先清理失效 PID 檔
2. 啟動前先找既有行程
3. 只在子行程確認仍存活時才寫入新的 PID 檔

`scripts/99_cleanup.sh` 也使用 `pgrep` 回退邏輯，所以即使 PID 檔不可信，仍能找到真正的行程。

目前判斷流程：

```text
讀 PID 檔
   |
   +--> PID 存在且行程活著：拒絕重複啟動
   |
   +--> PID 失效：刪除舊 PID 檔
           |
           v
用 pgrep 補查是否有主程式仍在跑
           |
           +--> 有：拒絕重複啟動
           |
           +--> 沒有：允許啟動
```

#### 為什麼這樣解決

- `kill -0` 可以確認 PID 是否仍存在。
- `pgrep -f` 可以在 PID 檔失效時補找主程式。
- 啟動後等待 1 秒再檢查存活，可以避免把初始化失敗的短命行程寫進 PID 檔。

#### 驗證方式

```bash
./scripts/03_run.sh background
./scripts/03_run.sh background
```

第二次會直接被拒絕，並提示先執行 `scripts/99_cleanup.sh`。這代表腳本在真正啟動新行程前，就已經擋下重複啟動。

#### 目前效果與延伸探討

- PID 檔只是輔助資訊，實際行程狀態仍要查系統。
- 背景啟動腳本如果沒有做存活驗證，會造成 PID 檔與實際行程資訊不一致。
- `nohup ... &` 成功不代表服務初始化成功，背景模式一定要補一段啟動後檢查。
- 若之後要做更嚴格的 readiness check，可在背景啟動後再呼叫 `GET /redfish/v1`；目前腳本尚未做 HTTP 健康檢查。

### 問題情境 2：前景模式關閉時，HTTP 接收執行緒需要可中斷

#### 問題現象

前景模式執行時按 `Ctrl+C`，日誌會出現：

```text
[info] Received shutdown signal 2
```

如果 HTTP 接收執行緒停在不可中斷的阻塞式 `accept()`，主執行緒在 `join()` 時會等不到它離開。

#### 發生條件

- 使用前景模式執行。
- 按 `Ctrl+C` 觸發 `SIGINT`。
- `Application::stop()` 進入 `RedfishApiServer::stop()`。

#### 根本原因

關閉流程除了更新 `running_` 旗標，也要讓接收執行緒離開等待中的 socket 操作。如果接收執行緒沒有機會回到 while 條件檢查，`join()` 就會等待。

這個問題的關鍵在阻塞式 I/O (blocking I/O)。`SIGINT` 可以被收到，`running_` 也可以被改成 `false`，但接收連線的執行緒仍需要被喚醒或被關閉。

#### 除錯方式

先看日誌是否印出 shutdown signal，再檢查程式是否仍存在：

```bash
pgrep -af ai-bmc-manager
```

若 shutdown signal 已出現，但行程仍停留，優先檢查背景執行緒停止路徑。

#### 解決方法

目前 `RedfishApiServer` 使用可中斷的關閉模式：

1. acceptor 設為 non-blocking
2. `stop()` 先 `cancel()` 再 `close()`
3. `acceptLoop()` 遇到 `operation_aborted` 或 `bad_descriptor` 時直接跳出
4. 對 `would_block` 做短暫 sleep，讓執行緒能重新檢查 `running_`

目前關閉路徑：

```text
Ctrl+C
  |
  v
signal handler 設定停止旗標
  |
  v
Application.stop()
  |
  v
RedfishApiServer.stop()
  |
  +--> running_ = false
  +--> acceptor.cancel()
  +--> acceptor.close()
  +--> acceptThread.join()
```

#### 為什麼這樣解決

`cancel()` 與 `close()` 會讓等待中的 accept loop 收到錯誤狀態；non-blocking acceptor 則讓迴圈有機會定期檢查 `running_`。這兩者合在一起，可以讓 `stop()` 的 `join()` 有可預期的結束路徑。

#### 驗證方式

用前景模式啟動後按 `Ctrl+C`。預期結果是日誌印出 shutdown 訊息後，shell 回到提示字元，不需要另外用 `kill -9`。

#### 目前效果與延伸探討

- 收到訊號不等於關閉流程一定順利。
- 任何 `join()` 前都要確認被等的執行緒有機會離開阻塞點。
- 使用阻塞式 I/O 的背景執行緒，需要設計可中斷的停止路徑。
- 同樣原則也適用於未來新增的背景 worker；若該 worker 會等待外部事件，需要同步設計停止旗標與喚醒方式。

### 問題情境 3：看起來像錯誤的 system bus 權限警告

#### 問題現象

啟動時會看到：

```text
Failed to request D-Bus service name on system bus (Permission denied), falling back to user bus
```

這行訊息需要判讀；它代表 system bus 註冊失敗，不代表整個服務啟動失敗。

#### 發生條件

- 使用一般使用者帳號執行。
- 沒有安裝 system bus policy 允許該使用者持有 `xyz.openbmc_project.AIServer`。

#### 根本原因

在一般桌面 Ubuntu 工作階段，若沒有安裝對應的 D-Bus policy，使用者帳號不能在 `system bus` 取得新的服務名稱。這是 D-Bus 權限模型的正常限制。

OpenBMC 類型系統在系統層級執行服務時，需要搭配 D-Bus policy 允許特定服務名稱。這個專案是開發驗證環境，預設不安裝 system bus policy，所以一般使用者直接執行時拿不到 `system bus` 名稱是預期結果。

#### 除錯方式

先看下一行是否出現：

```text
D-Bus bridge started on user bus
```

如果有，代表回退成功。也可以查 `BusMode` 屬性確認目前使用哪個 bus。

#### 解決方法

目前程式有回退機制：

1. 先試 `system bus`
2. 失敗時改用 `user bus`

後續若看到：

```text
D-Bus bridge started on user bus
```

就表示服務仍然正常。

這裡的設計取捨是：開發環境優先保留可執行路徑，並由 `BusMode` 屬性標示目前使用 `system` 或 `user`。

#### 為什麼這樣解決

正式 system bus 需要 policy；開發環境不一定有這些設定。回退到 user bus 可以保留本機 D-Bus 物件模型與 `busctl --user` 驗證能力。

#### 驗證方式

```bash
busctl --user get-property \
  xyz.openbmc_project.AIServer \
  /xyz/openbmc_project/ai/server \
  xyz.openbmc_project.AIServer.Server BusMode
```

如果回傳 `user`，代表目前使用使用者匯流排；這在一般開發環境是正常的。

#### 目前效果與延伸探討

- 先判斷「這是錯誤」還是「這是預期內的回退」。
- 開發文件需要寫明這種訊息的判讀方式，避免把回退訊息誤判為啟動失敗。
- 若要以 system bus 模式長期執行，需另外提供 D-Bus policy；目前專案只提供 systemd service 範例，沒有附 system bus policy 檔。

## 驗證結果

目前專案內的驗證來源主要是單元測試與 Demo 腳本。這裡只列出程式碼中已存在的驗證，不補寫未執行或不存在的量測結果。

| 驗證項目 | 檔案 | 驗證重點 |
| --- | --- | --- |
| 設定檔解析 | `tests/ProfileParsingTest.cpp` | 可讀取 `config/ai_server_profile.json`，並確認 GPU、Fan、PSU、NVMe、CPU 數量與重要欄位 |
| 散熱策略 | `tests/ThermalManagerTest.cpp` | `<70C`、`70-85C`、`>85C` 的 PWM 行為，以及過溫事件與降頻 |
| 功耗策略 | `tests/PowerManagerTest.cpp` | 超過功耗預算時，`BudgetExceeded`、`PowerCapActive`、GPU 降頻與事件記錄 |
| 事件記錄 | `tests/EventLoggerTest.cpp` | 多執行緒寫入事件後，事件數量與最新事件 ID |
| 故障注入 | `tests/FaultInjectionManagerTest.cpp` | 風扇故障、NVMe 故障與清除故障流程 |
| Redfish Demo | `scripts/04_demo_redfish.sh` | 根節點、系統、散熱、功耗、事件與更新服務端點 |
| Fault Demo | `scripts/05_demo_fault_injection.sh` | GPU 過溫、風扇、PSU、NVMe 故障與事件查詢 |

建置腳本 `scripts/02_build.sh` 會在編譯後執行：

```bash
ctest --test-dir build --output-on-failure
```

因此若該腳本完整通過，可以確認上述單元測試都已執行。若本機沒有安裝 `cmake`，會在 configure 階段失敗，需要先執行 `scripts/01_install_deps.sh` 或手動安裝必要套件。

## 限制事項

目前程式碼可確認的限制如下：

- HTTP API 受 Redfish 結構啟發，完整 Redfish 標準實作不在目前範圍內。
- 沒有實作 TLS、登入、Token、Role 或權限控管。
- 沒有 IPMI command、真實 sensor driver、實體韌體下載或燒錄。
- `FirmwareUpdateManager` 用 `image_uri` 字串內容模擬驗證與安裝失敗，不會讀檔或驗簽。
- D-Bus 目前只匯出 GPU 與 Fan sensor 物件；PSU、NVMe、CPU 沒有各自的 sensor object。
- `PowerConsumedWatts` 不含 CPU；PSU 輸出分攤含 CPU 基準負載。
- `EventLogger` 最多保存 512 筆事件，超過後移除最舊事件。
- HTTP 伺服器以 detached worker thread 處理連線，沒有連線池、請求佇列或高併發壓力測試資料。

## 後續可改善方向

這些項目是根據現有程式邊界整理出的可改善方向：

- 補上 TLS、認證與授權，避免 HTTP API 在非本機環境裸露。
- 明確定義 Redfish schema 相容範圍，或把文件與回應欄位固定標示為 schema-inspired。
- 將 PSU、NVMe、CPU 也匯出成 D-Bus sensor object，讓 D-Bus 與 HTTP 觀察範圍更一致。
- 將 `SimpleUpdate` 的空 `image_uri` 改成更直觀的 `400 Bad Request`，目前程式回 `409 Conflict`。
- 增加設定檔數值範圍驗證，例如溫度、PWM、PSU 額定功率。
- 補上 HTTP route 的單元測試或整合測試，避免路由與文件不同步。
- 針對 `PowerConsumedWatts` 是否應納入 CPU 建立明確規格，避免與 PSU 輸出模型混淆。

## 關鍵字整理

| 關鍵字 | 英文 | 說明 |
| --- | --- | --- |
| 硬體模型 | Hardware Model | 保存平台當前可變狀態的資料層 |
| 門面層 | Facade | 對上層提供固定入口的物件 |
| 物件模型 | Object Model | 資料如何以物件、屬性、介面表示 |
| 事件鎖存 | Event Latch | 避免同一故障每輪都重複送事件 |
| 降頻 | Throttling | 為了散熱或功耗限制而降低效能 |
| 功耗預算 | Power Budget | 平台允許的總功耗上限 |
| 回滾 | Rollback | 更新失敗後回到安全狀態 |
| 守護行程 | Daemon | 持續在背景執行的服務 |
| 使用者匯流排 | User Bus | 屬於使用者工作階段的 D-Bus |
| 系統匯流排 | System Bus | 系統層級共享的 D-Bus |
| 狀態機 | State Machine | 用明確階段描述流程，例如 Downloading、Verifying、Installing |
| 競態條件 | Race Condition | 多個動作時間順序不同，導致結果不一致 |
| PID 檔 | PID File | 記錄背景行程 ID 的檔案，只能當輔助資訊 |
| 阻塞式 I/O | Blocking I/O | 呼叫後會等待事件發生才回到呼叫端的輸入輸出操作 |
| 非阻塞式 I/O | Non-blocking I/O | 呼叫不會長時間卡住，可讓程式定期檢查停止條件 |
| 輸入正規化 | Input Normalization | 把不同寫法整理成同一種內部格式，例如 `0` 轉成 `gpu0` |

## 後續文件

- 想理解程式分層：看 [docs/architecture.md](docs/architecture.md)
- 想理解 HTTP 回應欄位：看 [docs/redfish-api.md](docs/redfish-api.md)
- 想理解 D-Bus 物件與 `busctl`：看 [docs/dbus-object-model.md](docs/dbus-object-model.md)
- 想理解功耗與散熱策略：看 [docs/power-policy.md](docs/power-policy.md) 與 [docs/thermal-policy.md](docs/thermal-policy.md)
