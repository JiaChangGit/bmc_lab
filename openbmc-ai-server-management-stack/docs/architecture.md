# 架構說明

## 1. 這份文件的範圍

這份文件只談目前程式裡真的存在的分層、執行緒、資料流與啟停順序。它不描述尚未實作的硬體驅動，也不假設完整 OpenBMC 環境。

## 2. 先釐清關鍵字

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| 架構 | Architecture | 描述系統如何分層、如何互相呼叫 | 本文件說明 `Application`、services、HTTP、D-Bus 與硬體模型的關係 |
| 服務門面 | Service Facade | 把多個內部服務整理成單一入口 | `ManagementService` 供 `RedfishApiServer` 與 `DbusBridge` 共同使用 |
| 狀態快照 | Snapshot | 某個時間點的狀態副本 | `PlatformSnapshot` 讓 API 與 D-Bus 讀到一致資料 |
| 背景執行緒 | Background Thread | 在主流程外持續處理工作 | `SensorService`、`DbusBridge`、`FirmwareUpdateManager` 都有背景工作 |
| 互斥鎖 | Mutex | 保護共享資料，避免多執行緒同時改寫 | `HardwareModel`、`EventLogger`、`FirmwareUpdateManager` 使用 mutex |
| 條件變數 | Condition Variable | 讓執行緒等待或被喚醒 | `SensorService` 用它支援固定輪詢與立即輪詢 |

## 3. 分層總覽

```text
外部工具
  ├─ curl
  ├─ busctl
  └─ tests
          |
          v
+---------------------------+
| RedfishApiServer          |
| DbusBridge                |
+-------------+-------------+
              |
              v
+---------------------------+
| ManagementService         |
+-------------+-------------+
              |
    +---------+---------+-----------------------+
    |                   |                       |
    v                   v                       v
SensorService   FirmwareUpdateManager   FaultInjectionManager
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

## 4. 每一層在做什麼

### 4.1 `HardwareModel`

`src/hardware/HardwareModel.*`

這一層保存平台當前狀態：

- GPU 溫度、功耗、健康狀態、是否降頻
- 風扇 RPM、PWM、是否故障
- PSU 輸出功率、健康狀態
- NVMe 溫度與健康狀態
- 目前的功耗遙測結果

這層也提供故障注入入口，例如：

- `injectGpuOverTemp`
- `injectFanFailure`
- `injectPsuFailure`
- `injectNvmeFault`

### 4.2 `SensorService`

`src/services/SensorService.*`

這一層負責週期性更新資料。它在背景執行緒內每秒執行一次：

1. `HardwareModel::simulateSensorTick()`
2. `HealthMonitor::evaluate()`
3. `ThermalManager::evaluate()`
4. 再抓一次 snapshot
5. `PowerManager::evaluate()`
6. 呼叫回呼，通知 D-Bus 更新屬性

順序不能隨便改，原因是：

- 先更新感測值
- 再判斷健康與散熱
- 最後用新的散熱結果計算功耗

### 4.3 `HealthMonitor`

`src/services/HealthMonitor.*`

它只做事件判斷，不直接修改硬體值。這一層根據 snapshot 檢查：

- 風扇是否故障
- PSU 是否失效
- NVMe 健康狀態是否異常

如果異常，會寫事件到 `EventLogger`。

### 4.4 `ThermalManager`

`src/services/ThermalManager.*`

它根據目前最高 GPU 溫度決定全機風扇 PWM。程式目前使用的是共用風扇策略：

- `< 70C` -> `40%`
- `70C 到 85C` -> `70%`
- `> 85C` -> `100%`

它也會在超過 `85C` 時寫入 `GPU_OVER_TEMP` 事件。

### 4.5 `PowerManager`

`src/services/PowerManager.*`

它會把 GPU、風扇、NVMe 的功耗彙總成 `TotalPower`，再和 `system_power_budget_watts` 比較。

如果超過上限，它會：

- 把 `BudgetExceeded` 設成 `true`
- 啟用 `PowerCapActive`
- 讓 `HardwareModel` 把所有 GPU 標成降頻
- 只在第一次超標時記錄 `POWER_CAP_TRIGGERED`

### 4.6 `EventLogger`

`src/services/EventLogger.*`

這一層只做兩件事：

1. 保存事件
2. 在事件新增後通知回呼

`DbusBridge` 會註冊回呼，因此每次有新事件時，D-Bus 也會同步送出 `EventGenerated`。

### 4.7 `FirmwareUpdateManager`

`src/services/FirmwareUpdateManager.*`

這一層用背景執行緒模擬韌體更新狀態機。它目前的狀態流程是：

```text
Idle
  -> Downloading
  -> Verifying
  -> Installing
  -> RebootPending
  -> Completed
```

若驗證或安裝失敗，會改走：

```text
Rollback
```

失敗條件是根據 `image_uri` 字串判斷：

- 含 `bad`
- 含 `verify-fail`
- 含 `corrupt`
- 含 `install-fail`
- 含 `fail-install`

### 4.8 `FaultInjectionManager`

`src/services/FaultInjectionManager.*`

這層負責兩件事：

1. 呼叫 `HardwareModel` 改變狀態
2. 透過回呼要求 `SensorService` 立即跑一次週期

這樣事件來源就會和一般感測流程一致。

### 4.9 `ManagementService`

`src/services/ManagementService.*`

這一層是對外的統一入口，供：

- `RedfishApiServer`
- `DbusBridge`

共同使用。

它提供的主要能力是：

- 取得整體 snapshot
- 讀取事件記錄
- 觸發故障注入
- 觸發韌體更新

### 4.10 `DbusBridge`

`src/dbus/DbusBridge.*`

這層把平台狀態映射成 D-Bus 物件。它會：

1. 先嘗試連到 `system bus`
2. 若無法取得服務名稱，就改用 `user bus`
3. 註冊 server、power、event 與 sensor 物件
4. 在背景執行緒中跑 `sd_bus_process()` / `sd_bus_wait()`

### 4.11 `RedfishApiServer`

`src/redfish/RedfishApiServer.*`

它負責：

- 監聽 `0.0.0.0:8080`
- 解析 HTTP 路徑
- 呼叫 `ManagementService`
- 組成 JSON 回應

目前所有路由都在同一個檔案內處理，便於對照 API 路由與 JSON 組裝邏輯。

## 5. 啟動順序

啟動順序定義在 `src/app/Application.cpp`。

```text
Application::start()
    -> dbusBridge_->start()
    -> sensorService_->start()
    -> redfishApiServer_->start()
    -> sensorService_->requestImmediateCycle()
```

### 啟動順序原因

- 先啟動 D-Bus，讓後續屬性更新有地方可送。
- 再啟動感測服務，讓硬體狀態開始流動。
- 再開 HTTP，避免 API 開始對外前，內部服務還沒準備好。
- 最後立刻要求一次輪詢，讓第一批資料不要等滿一個週期才出現。

## 6. 關閉順序

關閉順序同樣在 `src/app/Application.cpp`：

```text
Application::stop()
    -> redfishApiServer_->stop()
    -> sensorService_->stop()
    -> dbusBridge_->stop()
```

這是反向停止：

- 先停止新的外部請求進來
- 再停止背景感測更新
- 最後關閉 D-Bus

這樣可以降低在關閉途中還有新請求讀取半套狀態的機會。

## 7. 執行緒模型

目前程式中的主要執行緒如下：

| 執行緒 | 來源 | 用途 |
| --- | --- | --- |
| 主執行緒 | `main()` | 啟動系統、等待訊號、執行停止流程 |
| 感測執行緒 | `SensorService` | 每秒更新一次感測狀態與策略 |
| D-Bus 執行緒 | `DbusBridge` | 處理 `sd-bus` 的 process/wait 迴圈 |
| 韌體更新執行緒 | `FirmwareUpdateManager` | 模擬更新狀態機 |
| HTTP 接收執行緒 | `RedfishApiServer` | 監聽 TCP 連線 |
| HTTP 工作執行緒 | `RedfishApiServer` | 每個連線各自處理一次請求 |

## 8. 同步機制

### 8.1 互斥鎖

`HardwareModel`、`EventLogger`、`FirmwareUpdateManager` 都用互斥鎖保護共享狀態。

### 8.2 條件變數

`SensorService` 使用條件變數讓背景執行緒：

- 等待下一次週期
- 或在故障注入後立即被喚醒

### 8.3 事件鎖存

`HealthMonitor` 與 `ThermalManager` 使用集合記住目前已經記錄過的故障，避免同一故障每秒都再寫一次事件。

## 9. 兩條最重要的資料路徑

### 9.1 讀取路徑

```text
HTTP GET
  -> RedfishApiServer
  -> ManagementService::getPlatformSnapshot()
  -> HardwareModel::snapshot()
  -> JSON response
```

### 9.2 狀態變更路徑

```text
HTTP POST /api/fault/...
  -> FaultInjectionManager
  -> HardwareModel 狀態變更
  -> SensorService 立即輪詢
  -> Manager 判斷
  -> EventLogger
  -> D-Bus / Redfish 觀察到新結果
```

## 10. 開發注意事項

### 10.1 D-Bus 權限

程式會先試 `system bus`，失敗後回退到 `user bus`。在未安裝 system bus policy 的一般 Ubuntu 開發環境中，這個回退路徑可保留 D-Bus Demo 與 `busctl --user` 驗證能力。

### 10.2 背景啟動保護

`scripts/03_run.sh` 會檢查：

- 舊 PID 檔是否失效
- 是否已有相同行程
- 新行程在啟動 1 秒後是否仍存活

### 10.3 HTTP 關閉流程

`RedfishApiServer` 使用 non-blocking acceptor。原因是關閉流程需要讓接收執行緒離開 accept loop；若 accept loop 無法離開，`stop()` 內的 `join()` 會等不到執行緒結束。

## 11. 架構限制

- `RedfishApiServer` 提供的是受 Redfish 結構啟發的 HTTP/JSON 介面，未實作完整 Redfish schema。
- `DbusBridge` 目前只註冊 Server、Power、Events、GPU sensors 與 Fan sensors。
- `HardwareModel` 是模擬資料來源，沒有連到真實硬體。
- HTTP worker thread 以 detached thread 處理單次連線，沒有額外的連線池或併發壓力測試資料。
