# 設計決策與限制（Design Decisions and Limitations）

本文件記錄目前實作採用的設計與直接影響。未列出尚未排入實作的擴充藍圖。

## 私有 D-Bus Session Bus

服務由 `dbus-run-session` 啟動，使用 `libsystemd` 的 sd-bus API。這讓三個程序
能在一般 Linux user session 中執行，不需要修改 system bus policy。

影響：

- Demo 不需要 root。
- D-Bus service 與 objects 只存在於該 session。
- 無法直接取代 OpenBMC system bus 上的 ObjectMapper、sensor service 或
  logging service。
- `runtime/dbus-session.env` 只供同一次 session 的工具連線。

## 自訂 Manager 介面

`xyz.openbmc_project.MiniBMC.Manager` 提供 `ListObjects`、`GetObject` 與
`InjectFault`。前兩者使用 JSON string 傳遞 property snapshot，簡化 HTTP
service 的讀取流程。

影響：

- 不需要在 client 端動態遍歷多個 interface。
- 介面不是 OpenBMC ObjectMapper API。
- JSON string 失去原生 D-Bus variant type 的型別約束。

## HTTP service 採精簡 Redfish 風格

HTTP route 與 JSON 使用部分 Redfish resource 名稱和欄位，但 server 沒有
authentication、TLS、schema registry、ETag、account service 或完整 resource
model。

這個設計只用來驗證「D-Bus snapshot 到 HTTP JSON」的資料邊界。文件與測試將
它稱為 Redfish 風格 API，不宣告 conformance。

## UDS 取代實體 MCTP transport

MCTP client/server 使用 `AF_UNIX` + `SOCK_SEQPACKET`。MCTP packet header、
fragmentation、message tag 與 reassembly 仍在 repository 內實作並測試。

影響：

- 可在沒有 MCTP controller 的主機執行。
- 可以穩定注入 packet loss、out-of-order、sequence mismatch 與 timeout。
- 不能直接連接 Linux AF_MCTP 或外部 MCTP endpoint。

## PLDM 採 repository-local serialization

Type 0/2 command 名稱與流程參考 PLDM 概念，但 PDR 與 reading payload 是
repository-local encoding，numeric value 直接使用 little endian `double`。

影響：

- endpoint 與 sensor service 可共享簡單、可測試的 encoder/decoder。
- `SetEventReceiver` 與 `PlatformEventMessage` 只回傳 completion code。
- command payload 不能視為 DMTF PLDM wire-compatible。
- `SetFault` (`0xf0`) 是測試用 project-specific command。

## 每秒 polling 與 snapshot API

`SensorManager` 每秒更新 sensor 與 inventory snapshot。HTTP request 只讀取
D-Bus 上最近一次發布的值，不同步等待 PLDM 或 sysfs。

影響：

- HTTP latency 不受單次 PLDM timeout 直接影響。
- fault 注入後最多需要約一個 polling cycle 才會反映在 Health 與 EventLog。
- sensor freshness 目前只以 `LastUpdated` property 表示，沒有 stale timeout
  policy。

## Threshold 狀態保存在程序記憶體

`ThresholdEventEngine` 保存每個 sensor 的 active fault，並用 hysteresis
避免同一狀態每秒重複建立 event。

影響：

- assertion 與 recovery 可在同一次程序生命週期內配對。
- service 重啟後 active fault 與 event sequence 會重設。
- EventLog 沒有 persistence。

## Hardware provider 採合成資料

`mini_pcie_telemetry` 建立 class device、character device、ioctl 與 poll wait
queue，但不註冊 PCI driver，也不讀取實體 GPU。

`mini_i2c_hwmon` 透過 platform device 註冊 hwmon attributes，但不建立
`i2c_client`，也不執行 I2C transaction。

影響：

- Kernel ABI 可以在沒有專用硬體的 VM 或主機驗證。
- module 輸出不能當作實體硬體 telemetry。
- module runtime 仍受 kernel vermagic、module loading policy 與 sudo 權限
  限制。

## PCIe fallback 使用第一個 host PCI device

當 synthetic PCIe sysfs 不存在時，`SensorManager` 掃描 host PCI sysfs，並取
`std::filesystem::directory_iterator` 回傳的第一個有效 BDF 建立
`PCIeDevice0`。目前沒有額外排序或裝置選擇規則。

這個 fallback 只提供可查詢的 inventory 範例，不會辨識 GPU ownership，也不會
將所有 host PCI devices 發布到 D-Bus。

## D-Bus build stub

缺少 `libsystemd-dev` 時，CMake 仍建置 `mini_dbus` stub，讓不依賴 runtime bus
的單元測試可編譯。

影響：

- `scripts/build.sh` 可能成功，但 `run_session.sh` 仍會失敗。
- 要執行完整 session，必須安裝 `libsystemd-dev` 後重新 configure/build。

## Kernel headers fallback

`build_kernel_modules.sh` 優先使用 `/lib/modules/$(uname -r)/build`。不存在時，
才選擇 `/usr/src/linux-headers-*-generic` 中版本最高者做 compile-only
validation。

這個 fallback 不會讓 module 變得可載入。`load_kernel_modules.sh` 會先比較
vermagic，避免對錯誤 kernel 執行 `insmod`。

## Structured log 為單一 append-only JSONL

所有 service 寫入同一個 JSONL path。格式容易用 `jq` 或 `trace_cli` 讀取，但
目前沒有：

- 多程序 file locking
- log rotation
- retention
- durable delivery
- trace/span identifier

因此 `runtime/logs/mini-openbmc-service.jsonl` 適合本機 demo 與除錯，不適合作
為長期稽核儲存。
