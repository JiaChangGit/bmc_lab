# OpenBMC AI 伺服器管理堆疊

## 1. 專案目的

這個專案實作一套可在 Ubuntu apt 環境建置與執行的 BMC 管理堆疊 (management stack)，用 `HardwareModel` 模擬 AI 伺服器管理功能，並保留可直接觀察與除錯的模組分層。完整 OpenBMC 發行版、AST2600 韌體映像與真實硬體控制不在目前範圍內。

這份專案處理四件事：

1. 把平台狀態 (platform state) 與管理邏輯 (management logic) 分開。
2. 用 D-Bus 與受 Redfish 結構啟發的 HTTP/JSON 介面，同時對外呈現同一份狀態。
3. 模擬本專案涵蓋的管理情境，例如過溫、功耗超標、故障事件與韌體更新。
4. 保留程式碼中可確認的問題處理情境，例如背景啟動、PID 檔、D-Bus 權限、`Ctrl+C` 關閉流程。

整體分工如下：

- `HardwareModel` 模擬硬體
- `services` 負責判斷規則
- `DbusBridge` 把資料發到 D-Bus
- `RedfishApiServer` 把資料整理成 HTTP/JSON API

文件脈絡：

1. `README.md`：建置、執行與 Demo 流程。
2. `report_bmc_api.md`：API、資料流與程式碼可確認的問題情境。
3. `docs/`：Redfish、D-Bus、散熱與功耗策略等專題文件。

這份專案的重點是讓每一層的責任可以被追蹤。當 API 回傳某個欄位時，可以一路追到服務層、硬體模型與事件記錄，避免只停留在 JSON 表面欄位。

## 2. 這個專案解決什麼問題

本專案模擬的目標平台是一台高密度 AI 伺服器，包含：

- 8 張 GPU
- 8 顆風扇
- 4 顆 PSU
- 16 顆 NVMe
- 2 顆 CPU

本專案整理的管理需求是：

| 管理需求 | 英文 | 專案中怎麼做 |
| --- | --- | --- |
| 散熱控制 | Thermal Control | 根據最高 GPU 溫度調整全機風扇 PWM |
| 功耗限制 | Power Capping | 計算總功耗並在超標時啟動 GPU 降頻 |
| 故障偵測 | Fault Detection | 偵測風扇、PSU、NVMe 的異常狀態 |
| 事件記錄 | Event Logging | 把重要狀態變化記錄成事件並對外提供查詢 |
| 韌體更新流程 | Firmware Update Workflow | 模擬下載、驗證、安裝、回滾 |
| 對外管理介面 | Management Interface | 提供受 Redfish 結構啟發的 HTTP API 與 D-Bus 物件模型 |

這裡的「管理」指 BMC 在主機旁邊持續觀察硬體狀態，並提供固定介面讓外部管理工具查詢或觸發動作。例如資料中心管理系統可透過 Power API 查詢「這台伺服器功耗是否超標」，不需要直接讀每一張 GPU 的內部資料。這就是 BMC 管理堆疊存在的原因。

名詞對照：

| 名詞 | 英文 | 在本專案的意思 |
| --- | --- | --- |
| 平台 | Platform | 這台模擬 AI 伺服器的整體硬體集合 |
| 遙測 | Telemetry | 溫度、功耗、風扇轉速等可量測資料 |
| 策略 | Policy | 遇到某種狀態時要做的判斷規則，例如過溫時拉高風扇 |
| 事件 | Event | 管理器判定需要保存的狀態變化 |
| 狀態快照 | Snapshot | 某一個時間點的硬體與服務狀態副本 |

## 3. 功能架構

### 3.1 功能分層圖

```text
外部使用者 / 工具
  ├─ curl
  ├─ busctl
  ├─ demo scripts
  └─ unit tests
          |
          v
+----------------------------------------------+
| 對外介面層 (Interface Layer)                 |
|  - RedfishApiServer                          |
|  - DbusBridge                                |
+----------------------+-----------------------+
                       |
                       v
+----------------------------------------------+
| 服務門面層 (Service Facade Layer)            |
|  - ManagementService                         |
+----------------------+-----------------------+
                       |
     +-----------------+------------------+------------------+
     |                                    |                  |
     v                                    v                  v
SensorService                   FirmwareUpdateManager   FaultInjectionManager
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

### 3.2 每個元件負責什麼

| 元件 | 英文 | 職責 |
| --- | --- | --- |
| `HardwareModel` | Hardware Model | 保存 GPU、風扇、PSU、NVMe、CPU 的可變狀態 |
| `SensorService` | Sensor Service | 每秒推進一次感測資料與策略判斷 |
| `HealthMonitor` | Health Monitor | 偵測風扇、PSU、NVMe 的故障 |
| `ThermalManager` | Thermal Manager | 根據 GPU 溫度調整風扇策略 |
| `PowerManager` | Power Manager | 計算總功耗與是否超出預算 |
| `EventLogger` | Event Logger | 保存事件並通知其他元件 |
| `FaultInjectionManager` | Fault Injection Manager | 讓外部 API 能夠人工製造故障情境 |
| `FirmwareUpdateManager` | Firmware Update Manager | 模擬非同步韌體更新工作流程 |
| `ManagementService` | Service Facade | 統一提供上層查詢與動作入口 |
| `DbusBridge` | D-Bus Bridge | 把服務層狀態發布成 D-Bus 物件與屬性 |
| `RedfishApiServer` | Redfish-style HTTP API Server | 把服務層狀態整理成 HTTP/JSON 回應；本專案未宣告完整 Redfish 標準相容 |

### 3.3 一次故障注入會怎麼流動

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
        +--> D-Bus 發送 EventGenerated
        |
        +--> Redfish Event Log 可查到新事件
```

這張圖的關鍵在於：故障注入 API 先改變硬體狀態，再讓感測服務跑完整條策略路徑。事件來源與一般感測流程一致，可避免「API 看起來有變，但內部狀態沒有同步」的問題。

### 3.4 為什麼要有 `ManagementService`

`ManagementService` 是服務門面 (service facade)。它把上層入口統一收進來：

```text
RedfishApiServer  ----+
                      +--> ManagementService --> HardwareModel / EventLogger / Managers
DbusBridge       -----+
```

這樣設計有兩個實際原因：

1. HTTP 與 D-Bus 不需要各自知道硬體模型細節。
2. 之後如果要改功耗或散熱策略，不需要同時修改兩種對外介面。

職責分界是：HTTP API 與 D-Bus 負責對外呈現狀態，`ManagementService` 負責取得同一份服務層狀態。

## 4. 程式碼架構

### 4.1 目錄結構

```text
openbmc-ai-server-management-stack/
├── README.md
├── report_bmc_api.md
├── CMakeLists.txt
├── config/
│   └── ai_server_profile.json
├── docs/
│   ├── architecture.md
│   ├── dbus-object-model.md
│   ├── redfish-api.md
│   ├── thermal-policy.md
│   ├── power-policy.md
│   └── design-notes.md
├── scripts/
│   ├── 01_install_deps.sh
│   ├── 02_build.sh
│   ├── 03_run.sh
│   ├── 04_demo_redfish.sh
│   ├── 05_demo_fault_injection.sh
│   ├── 98_distclean.sh
│   └── 99_cleanup.sh
├── src/
│   ├── app/
│   ├── common/
│   ├── dbus/
│   ├── hardware/
│   ├── redfish/
│   ├── services/
│   └── main.cpp
└── tests/
```

### 4.2 各目錄用途

| 目錄 | 用途 |
| --- | --- |
| `config/` | 平台初始設定，例如 GPU 數量、風扇數量、系統功率預算 |
| `docs/` | 各主題說明文件 |
| `scripts/` | 安裝、建置、執行、示範、清理腳本 |
| `src/app/` | `Application` 組裝與生命週期管理 |
| `src/common/` | 共用資料型別與時間工具 |
| `src/hardware/` | 硬體模型與平台設定檔載入 |
| `src/services/` | 管理邏輯、策略、事件與工作流程 |
| `src/dbus/` | D-Bus 物件與屬性匯出 |
| `src/redfish/` | HTTP 路由、JSON 回應與動作 API |
| `tests/` | 單元測試 |

### 4.3 幾個最重要的程式檔案

| 檔案 | 為什麼重要 |
| --- | --- |
| `src/main.cpp` | 看主程式怎麼處理參數、訊號與啟停 |
| `src/app/Application.cpp` | 看整個系統的組裝順序與停止順序 |
| `src/hardware/HardwareModel.cpp` | 看感測值怎麼模擬、故障怎麼注入 |
| `src/services/SensorService.cpp` | 看週期性輪詢怎麼運作 |
| `src/services/ThermalManager.cpp` | 看風扇策略怎麼判斷 |
| `src/services/PowerManager.cpp` | 看功耗預算怎麼計算 |
| `src/dbus/DbusBridge.cpp` | 看 system bus / user bus 回退邏輯 |
| `src/redfish/RedfishApiServer.cpp` | 看所有 API 路由與回傳格式 |

## 5. 關鍵字速查

| 中文 | 英文 | 一般用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| 基板管理控制器 | Baseboard Management Controller, BMC | 管理伺服器硬體狀態的控制器 | 以一般 Linux 程式模擬 BMC 管理堆疊 |
| Redfish 風格 API | Redfish-style API | 以 HTTP/JSON 呈現硬體管理資源 | `RedfishApiServer` 使用類似 Redfish 的路徑與 JSON 形狀；未宣告完整標準相容 |
| D-Bus | Desktop Bus | 本機行程間通訊 (IPC) | `DbusBridge` 匯出 Server、Power、Event、GPU sensor 與 Fan sensor 物件 |
| 物件模型 | Object Model | 用物件、屬性、介面表達系統狀態 | D-Bus 文件用它描述路徑、介面與屬性 |
| 門面層 | Facade | 對外提供單一固定入口 | `ManagementService` 統一供 HTTP 與 D-Bus 查詢狀態 |
| 守護行程 | Daemon | 長時間在背景運作的服務 | `ai-bmc-manager` 可用前景或背景模式執行 |
| 故障注入 | Fault Injection | 人工製造錯誤情境以驗證系統反應 | `/api/fault/...` 改變 `HardwareModel` 狀態並喚醒感測週期 |
| 功耗預算 | Power Budget | 系統允許的最大功耗上限 | `PowerManager` 用 `system_power_budget_watts` 判斷是否超標 |
| 降頻 | Throttling | 因散熱或功耗限制而降低效能 | `HardwareModel` 依過溫或功耗上限標記 GPU `throttled` |
| 韌體更新流程 | Firmware Update Workflow | 韌體下載、驗證、安裝與回滾的流程 | `FirmwareUpdateManager` 用背景執行緒模擬狀態機 |
| 匯流排 | Bus | D-Bus 的通訊通道 | 先試 `system bus`，失敗後回退到 `user bus` |
| 事件鎖存 | Event Latch | 避免同一事件每輪都重複記錄 | `HealthMonitor`、`ThermalManager`、`PowerManager` 用它控制事件重複寫入 |
| 端點 | Endpoint | 一個可被呼叫的 API 路徑 | 例如 `/redfish/v1/Chassis/chassis/Power` |
| 請求本文 | Request Body / Payload | `POST` 時送給 API 的資料 | `SimpleUpdate` 使用 JSON 內的 `image_uri` |
| 輪詢 | Polling | 固定時間查一次狀態 | `SensorService` 每秒跑一輪，也可被故障注入立即喚醒 |
| 訊號 | Signal | D-Bus 對外通知事件或屬性變化 | `EventGenerated` 通知本機觀察者有新事件 |
| 回退 | Fallback | 第一種方式失敗後改用另一種方式 | D-Bus system bus 失敗時改用 user bus |

## 6. 建置流程

這一段會把「從乾淨環境到可執行檔」的流程拆開來看。

### 6.1 開發環境需求

建議環境：

- Ubuntu 22.04 / 24.04 類型的 apt 環境
- Bash
- `sudo` 權限
- 使用者工作階段可使用 D-Bus user bus

如果在 WSL 或一般桌面 Ubuntu 上執行，且使用者工作階段已啟用 D-Bus user bus，可用 `user bus` 完成示範。`system bus` 需要額外權限與服務設定；這份專案會先嘗試使用 `system bus`，失敗後自動回退到 `user bus`。這是預期行為，不代表功能壞掉。

### 6.2 安裝相依套件

```bash
chmod +x scripts/*.sh
./scripts/01_install_deps.sh
```

這個腳本實際做的事：

1. `apt-get update`
2. 安裝建置工具
3. 安裝 Boost、`sd-bus`、JSON、日誌與測試套件

主要套件與用途：

| 套件 | 用途 |
| --- | --- |
| `cmake` | 產生建置設定 |
| `ninja-build` | 執行編譯 |
| `build-essential` | 提供 `g++` 等編譯器工具 |
| `libboost-all-dev` | 提供 Boost.Asio 與 Boost.Beast |
| `libsystemd-dev` | 提供 `sd-bus` |
| `nlohmann-json3-dev` | JSON 庫 |
| `libspdlog-dev` | 日誌輸出 |
| `libgtest-dev` | 單元測試 |
| `dbus-user-session` | 使用者匯流排支援 |

### 6.3 建置

```bash
./scripts/02_build.sh
```

這個腳本依序做三件事：

```text
cmake configure
    ->
cmake build
    ->
ctest
```

如果你想知道每一步在做什麼，可以對照下面的手動版本：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$(nproc)"
ctest --test-dir build --output-on-failure
```

#### 每一步的目的

| 指令 | 目的 | 成果 |
| --- | --- | --- |
| `cmake -S . -B build ...` | 讀取 `CMakeLists.txt` 並產生建置規則 | `build/` 內的 Ninja 與 CMake 設定檔 |
| `cmake --build build ...` | 真的編譯程式 | `build/ai-bmc-manager` |
| `ctest --test-dir build ...` | 執行測試 | 確認核心邏輯未壞掉 |

#### 建置成功後你會得到什麼

- `build/ai-bmc-manager`：主程式
- `build/openbmc_ai_stack_tests`：測試執行檔
- `build/build.ninja`：Ninja 建置規則

如果 `./scripts/02_build.sh` 成功，代表三件事都通過：

1. 相依套件可以被 CMake 找到。
2. C++ 程式可以完整編譯與連結。
3. 單元測試確認核心策略沒有明顯回歸。

如果失敗，先看錯誤出在哪一段。`cmake -S` 失敗時先檢查相依套件與 CMake 設定；`cmake --build` 失敗時先檢查 C++ 編譯錯誤；`ctest` 失敗則代表程式可編譯，但行為和測試預期不一致。

### 6.4 設定方式

主程式預設讀取：

```text
config/ai_server_profile.json
```

這份設定檔由 `AIServerProfile::loadFromFile()` 解析，必要欄位包含：

- `system_power_budget_watts`
- `gpus`
- `fans`
- `psus`
- `nvmes`
- `cpus`

可以用啟動參數指定其他設定檔與 HTTP 埠：

```bash
build/ai-bmc-manager --config config/ai_server_profile.json --port 8080
```

設定檔用途：

| 欄位 | 用途 | 程式對應 |
| --- | --- | --- |
| `system_power_budget_watts` | 平台功耗預算 | `PowerManager` 超標判斷 |
| `gpus` | GPU 初始溫度、功耗與健康狀態 | `HardwareModel` GPU 狀態 |
| `fans` | 風扇初始 RPM 與 PWM | `ThermalManager` 後續調整 PWM |
| `psus` | PSU 額定功率與健康狀態 | `HardwareModel::refreshPsuOutputLocked()` |
| `nvmes` | NVMe 溫度與健康狀態 | `HealthMonitor` 故障判斷 |
| `cpus` | CPU 摘要資訊 | `/redfish/v1/Systems/system` 回應 |

注意事項：

- 設定檔缺少必要欄位時，載入會丟出錯誤並中止啟動。
- `system_power_budget_watts` 必須大於 `0`。
- 各硬體清單不可為空。
- 目前程式沒有對所有數值做完整範圍檢查，例如負溫度或不合理 PWM 仍需由設定檔維護者自行避免。

## 7. 執行流程

### 7.1 前景模式

```bash
./scripts/03_run.sh
```

前景模式適合：

- 你想直接看日誌
- 你想測 `Ctrl+C`
- 你正在除錯啟動或關閉流程

正常啟動後，日誌會包含下列類型的訊息：

```text
[warning] Failed to request D-Bus service name on system bus (Permission denied), falling back to user bus
[info] D-Bus bridge started on user bus
[info] Redfish API listening on 0.0.0.0:8080
[info] Application started
```

第一行代表系統無法在 `system bus` 取得服務名稱，所以改用 `user bus`。後續若出現 `started on user bus`，就代表啟動成功。

啟動成功後，HTTP API 會綁在 `0.0.0.0:8080`。在同一台機器測試時，用 `http://127.0.0.1:8080` 存取。`0.0.0.0` 是監聽位址，意思是接受本機可用網路介面的連線；`127.0.0.1` 是本機回環位址。

### 7.2 背景模式

```bash
./scripts/03_run.sh background
tail -f run/ai-bmc-manager.log
```

背景模式適合：

- 想一邊讓服務跑著，一邊另外開 shell 測 API
- 想保留日誌檔案

這個腳本做了幾件保護工作：

1. 建立 `run/`
2. 檢查 `build/ai-bmc-manager` 是否存在
3. 清理失效 PID 檔
4. 檢查是否已有同名行程正在執行
5. 只有在子行程確認仍存活時，才寫入新的 PID 檔

#### 為什麼要這麼做

背景啟動需要處理的問題是：

- 程式其實啟動失敗
- 但腳本已經先宣告成功
- 還把 PID 檔寫成錯的

這份腳本現在已經補上「啟動後驗證存活」這一層，因此第二次誤啟動不會再覆寫真正的 PID 檔。

### 7.3 停止背景模式

```bash
./scripts/99_cleanup.sh
```

這個腳本除了讀 PID 檔，也會用 `pgrep` 補找實際行程。原因是：

- PID 檔遺失
- PID 檔過期
- 行程仍在執行，但 PID 檔內容不可信

### 7.4 清理建置與執行產物

```bash
./scripts/98_distclean.sh
```

這個腳本會做兩件事：

1. 先呼叫 `./scripts/99_cleanup.sh`，停止背景服務並清理 PID / log
2. 刪除 `build/` 與 `run/`

適合使用的時機：

- 你準備整理工作目錄
- 你不想把建置產物或執行期日誌一起上傳
- 你要重新做一次乾淨建置

## 8. Demo 流程

專案提供兩條 Demo 路線，用來觀察服務啟動後的實際資料流：

1. Redfish 讀取與韌體更新示範
2. 故障注入與事件記錄示範

### 8.1 Redfish 示範流程

```bash
./scripts/04_demo_redfish.sh
```

這個腳本會依序打以下 API：

1. `GET /redfish/v1`
2. `GET /redfish/v1/Systems/system`
3. `GET /redfish/v1/Chassis/chassis/Thermal`
4. `GET /redfish/v1/Chassis/chassis/Power`
5. `GET /redfish/v1/Managers/bmc/LogServices/EventLog/Entries`
6. `GET /redfish/v1/UpdateService`
7. `POST /redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate`

#### 每一步要看什麼

| API | 重點欄位 | 你會知道什麼 |
| --- | --- | --- |
| `/redfish/v1` | `Systems`、`Chassis`、`Managers`、`UpdateService` | 服務根節點是否正常 |
| `/Systems/system` | `GpuCount`、`NvmeCount`、`PowerCapActive` | 平台摘要與功耗上限狀態 |
| `/Chassis/chassis/Thermal` | `ReadingCelsius`、`ReadingRPM`、`PwmPercent` | GPU 與風扇狀態 |
| `/Chassis/chassis/Power` | `PowerConsumedWatts`、`BudgetExceeded` | 功耗是否超標 |
| `/Managers/bmc/LogServices/EventLog/Entries` | `Members` | 目前有哪些事件 |
| `/UpdateService` | `FirmwareState`、`LastResult` | 韌體更新流程目前停在哪裡 |
| `SimpleUpdate` | `State`、`Message` | 韌體更新是否被接受 |

### 8.2 故障注入示範流程

```bash
./scripts/05_demo_fault_injection.sh
```

這個腳本會做以下動作：

1. 注入 `gpu0` 過溫
2. 注入 `fan0` 故障
3. 注入 `psu0` 故障
4. 注入 `nvme0` 故障
5. 讓其餘 GPU 也升溫，強迫走到功耗上限路徑
6. 查詢 Power API
7. 查詢 Event Log API
8. 清除所有故障

#### 為什麼這個流程有價值

它一次串起了四條邏輯：

- 硬體狀態改變
- 策略判斷
- 事件記錄
- API 對外呈現

這個流程用來驗證整個資料流是否一致，檢查範圍包含硬體模型、策略判斷、事件記錄與 API 回應。

#### 預期結果怎麼讀

跑完故障注入示範後，最少要確認三個地方：

| 查詢位置 | 預期結果 | 代表意義 |
| --- | --- | --- |
| Thermal API | `fan0` 的 `ReadingRPM` 變成 `0` 或 GPU 溫度升高 | 硬體模型已被故障注入改變 |
| Power API | `BudgetExceeded` 變成 `true` | 功耗策略有重新計算，且總功耗超過設定檔中的 `system_power_budget_watts` |
| Event Log API | 出現 `GPU_OVER_TEMP`、`FAN_FAILURE` 等事件 | 策略判斷有寫入事件記錄 |

如果只看到 API 成功回傳，但事件記錄沒有新增，代表該次故障注入沒有完成感測與策略判斷路徑。這也是本專案把 `FaultInjectionManager` 接到 `SensorService.requestImmediateCycle()` 的原因。

### 8.3 手動 Demo 版本

逐步操作可用下面指令：

```bash
curl -s http://127.0.0.1:8080/redfish/v1 | jq
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Thermal | jq
curl -s -X POST http://127.0.0.1:8080/api/fault/fan-failure/fan0 | jq
sleep 1
curl -s http://127.0.0.1:8080/redfish/v1/Managers/bmc/LogServices/EventLog/Entries | jq
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Power | jq
```

這組手動指令可以拆成三段理解：

1. 前兩行先確認服務與散熱資料可以查。
2. 第三行製造 `fan0` 故障，`sleep 1` 讓背景輪詢有時間更新狀態。
3. 最後兩行確認事件與功耗資料是否反映新的平台狀態。

### 8.4 API 說明總覽

本專案的 HTTP API 由 `src/redfish/RedfishApiServer.cpp` 實作。路徑採用 Redfish 常見的資源命名方式，但回應中也明確標示這是 `Schema-inspired`。它是開發驗證用的 HTTP/JSON 介面，完整 Redfish 標準實作不在目前範圍內。

API 類型：

| 類型 | 範例 | 用途 |
| --- | --- | --- |
| 讀取型 API | `GET /redfish/v1/Chassis/chassis/Power` | 查詢目前平台狀態，不改變內部資料 |
| 動作型 API | `POST /redfish/v1/UpdateService/Actions/UpdateService.SimpleUpdate` | 觸發非同步韌體更新流程 |
| 開發測試 API | `POST /api/fault/fan-failure/fan0` | 人工製造故障，用來觀察策略、事件與 API 輸出 |

API 回應來源：

```text
HTTP request
   |
   v
RedfishApiServer
   |
   v
ManagementService
   |
   v
HardwareModel / EventLogger / FirmwareUpdateManager
   |
   v
JSON response
```

若要看逐條欄位、錯誤狀態碼與 Redfish / IPMI 等比較，請看 [docs/redfish-api.md](docs/redfish-api.md) 與 [report_bmc_api.md](report_bmc_api.md)。

## 9. 啟動後驗證方式

### 9.1 驗證 HTTP API

```bash
curl -s http://127.0.0.1:8080/redfish/v1 | jq
```

這一步確認：

- HTTP 埠有沒有打開
- Redfish 路由有沒有掛上

服務正常時，回應會包含 `Systems`、`Chassis`、`Managers`、`UpdateService`。如果連不上，先確認服務是否仍在跑；如果連得上但欄位不完整，再回頭看 `RedfishApiServer.cpp` 的路由組裝。

### 9.2 驗證 D-Bus

```bash
busctl --user list | grep xyz.openbmc_project.AIServer
busctl --user tree xyz.openbmc_project.AIServer
```

這一步確認：

- 服務名稱有沒有註冊
- 物件路徑有沒有建立

本專案可觀察到的物件路徑包含：

```text
/xyz/openbmc_project/ai/server
/xyz/openbmc_project/ai/power
/xyz/openbmc_project/ai/events
/xyz/openbmc_project/ai/sensors/gpu0
/xyz/openbmc_project/ai/sensors/fan0
```

如果 `busctl --user list` 查不到 `xyz.openbmc_project.AIServer`，先看啟動日誌是否有 `D-Bus bridge started on user bus`。

### 9.3 驗證單元測試

```bash
ctest --test-dir build --output-on-failure
```

目前主要涵蓋：

- JSON 設定檔解析
- 散熱策略
- 功耗策略
- 事件記錄
- 故障注入

## 10. 除錯重點

### 10.1 `Permission denied` 的 D-Bus 警告

這代表程式沒拿到 `system bus` 的名稱，但後續會自動改用 `user bus`。看到下列訊息時：

```text
[info] D-Bus bridge started on user bus
```

就代表服務已正常啟動。

### 10.2 為什麼第二次背景啟動會被拒絕？

因為同一個 D-Bus 服務名稱不能被兩個程式行程同時持有，而且覆寫 PID 檔會讓後續清理變得不可靠。現在腳本會先擋住第二次啟動，這是保護機制。

### 10.3 為什麼有 HTTP API，還要有 D-Bus？

因為兩者用途不同：

- HTTP API 適合外部管理與 HTTP 工具
- D-Bus 適合本機服務整合與狀態除錯

僅使用 HTTP API 時，看不到內部 D-Bus 物件模型；僅使用 D-Bus 時，外部 HTTP 工具無法直接查詢。

## 11. 限制事項

目前程式碼可確認的限制如下：

- HTTP API 是受 Redfish 結構啟發的示範介面，未實作完整 Redfish schema、認證、授權或 TLS。
- 硬體狀態由 `HardwareModel` 模擬，沒有讀取真實 BMC 暫存器、I2C 裝置或 PCIe 裝置。
- 韌體更新是狀態機模擬，沒有下載檔案、驗證簽章或燒錄韌體。
- D-Bus 目前匯出 Server、Power、Events、GPU sensors 與 Fan sensors；PSU、NVMe、CPU 沒有各自的 D-Bus sensor 物件。
- `PowerConsumedWatts` 不含 CPU；PSU 輸出分攤含 CPU 基準負載。這是目前模型的計算邊界。
- 故障清除會重設 GPU、風扇、PSU、NVMe 的故障來源；功耗上限與遙測狀態會在後續感測與功耗週期中重新計算。
- `EventLogger` 最多保留 512 筆事件，超過後會移除最舊事件。

## 12. 延伸文件

| 文件 | 內容 |
| --- | --- |
| [report_bmc_api.md](report_bmc_api.md) | API 比較、資料流、問題情境與處理方式 |
| [docs/architecture.md](docs/architecture.md) | 系統架構、執行緒與同步說明 |
| [docs/redfish-api.md](docs/redfish-api.md) | 逐條 API 端點與欄位說明 |
| [docs/dbus-object-model.md](docs/dbus-object-model.md) | D-Bus 服務、物件、屬性與訊號 |
| [docs/thermal-policy.md](docs/thermal-policy.md) | 散熱策略規則與實例 |
| [docs/power-policy.md](docs/power-policy.md) | 功耗預算公式與超標處理 |
| [docs/design-notes.md](docs/design-notes.md) | 設計說明 |

## 13. 驗證清單

- 安裝相依套件：`./scripts/01_install_deps.sh`
- 建置與測試：`./scripts/02_build.sh`
- 前景執行：`./scripts/03_run.sh`
- 背景執行：`./scripts/03_run.sh background`
- Redfish 示範：`./scripts/04_demo_redfish.sh`
- 故障注入示範：`./scripts/05_demo_fault_injection.sh`
- D-Bus 驗證：`busctl --user tree xyz.openbmc_project.AIServer`
- 背景清理：`./scripts/99_cleanup.sh`
- 清理產物：`./scripts/98_distclean.sh`
