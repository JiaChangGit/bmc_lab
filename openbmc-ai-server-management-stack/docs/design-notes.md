# 設計說明

這份文件整理目前程式碼裡能確認的設計選擇，只描述已實作或可從原始碼追到的行為。

名詞對照：

| 中文 | 英文 | 功能用途 | 本專案中的用途 |
| --- | --- | --- | --- |
| 傳輸層 | Transport Layer | 處理資料如何進出系統 | `RedfishApiServer` 負責 HTTP |
| 服務門面 | Service Facade | 對外提供統一入口 | `ManagementService` 隔開 HTTP/D-Bus 與內部服務 |
| 硬體模型 | Hardware Model | 保存硬體狀態 | `HardwareModel` 模擬 GPU、Fan、PSU、NVMe、CPU |
| 阻塞式 I/O | Blocking I/O | 等待事件時會停住呼叫端 | HTTP accept loop 關閉流程需要避免停在不可中斷等待 |
| 事件鎖存 | Event Latch | 避免同一事件重複寫入 | Health、Thermal、Power manager 都有使用 |

## 1. HTTP 層與 `HardwareModel` 的邊界

HTTP 層如果直接操作 `HardwareModel`，會把「傳輸層」和「管理邏輯」混在一起。

目前程式的分工是：

- `RedfishApiServer`：解析路徑、組 JSON
- `ManagementService`：對外提供固定服務入口
- `HardwareModel`：保存狀態

如果 HTTP 層直接改硬體模型，之後再加 D-Bus 或其他入口時，就很難保證邏輯一致。

## 2. `ManagementService` 的位置

`ManagementService` 把外部入口和內部服務隔開。

目前兩個上層都依賴它：

- `RedfishApiServer`
- `DbusBridge`

這樣分層有兩個效果：

- 上層不需要知道底下有多少 manager
- 內部實作可以調整，但對外介面不必跟著大改

## 3. 故障注入不直接寫事件

直接寫事件會跳過真實邏輯路徑。

現在的流程是：

```text
注入故障
  -> 改變 HardwareModel
  -> 喚醒 SensorService
  -> 讓 manager 正常判斷
  -> EventLogger 記錄事件
```

這樣才能確認：

- 感測更新有沒有跑
- 策略判斷有沒有生效
- 事件是否真的由系統狀態推導出來

## 4. `SensorService` 在一次週期內抓兩次 snapshot

第一次 snapshot 用來給：

- `HealthMonitor`
- `ThermalManager`

接著 `ThermalManager` 會依最高 GPU 溫度更新風扇 PWM。這時候要再抓一次 snapshot，才能讓 `PowerManager` 用新的風扇狀態計算功耗。

## 5. `PowerManager` 不把 `TotalPsuPower` 算進 `TotalPower`

`TotalPsuPower` 是供電輸出的分攤結果，不能再當成額外耗電元件加回去。

如果把它再加進去，等於把同一份負載重算第二次。

## 6. D-Bus 先試 `system bus`，失敗再用 `user bus`

這個順序用來兼顧兩種情境：

- 若在系統層服務環境執行，可以使用 `system bus`
- 若在一般桌面開發環境執行，且沒有 system bus policy，需使用 `user bus`

目前程式會先試 `system bus`，失敗後再回退，這樣同一份程式在兩種環境都能啟動。

## 7. 背景模式啟動會檢查 PID 檔和現有行程

這段檢查可以避免兩種問題：

1. PID 檔存在，但實際行程已經不在
2. 實際行程還活著，但 PID 檔內容錯了

現在 `scripts/03_run.sh` 和 `scripts/99_cleanup.sh` 都會搭配 `kill -0` 與 `pgrep` 交叉確認，避免腳本誤判。

## 8. 前景模式按 `Ctrl+C` 時，HTTP 接收執行緒要能被中斷

HTTP 接收執行緒若停在不可中斷的阻塞式 `accept()`，`stop()` 裡的 `join()` 就會等不到它回來。

目前 `RedfishApiServer` 使用：

- acceptor non-blocking
- `stop()` 時先 `cancel()` 再 `close()`
- 遇到 `operation_aborted` 時跳出接收迴圈

這樣 `join()` 才能正常返回。

## 9. 事件鎖存避免重複寫入

如果不鎖存，像風扇壞掉這種持續性故障會每秒寫一次相同事件，日誌很快就失去可讀性。

鎖存的效果是：

- 第一次發生時記錄
- 故障持續期間不重複記錄
- 恢復後再發生，才重新記錄

## 10. 韌體更新流程用 `image_uri` 名稱判斷成功或失敗

這份程式的重點在流程控制，不在真實燒錄。

這種設計可用來確認：

- 狀態轉換
- 錯誤處理
- 回滾路徑
- 事件輸出

不需要真的準備韌體映像與簽章驗證環境。
