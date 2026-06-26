# 系統內部設計（System Internals）

本文件記錄目前程式碼的程序責任、資料模型與介面邊界。架構圖請參考
[architecture.md](architecture.md)。

## 程序與執行檔

| 程序 | 入口 | 主要責任 |
| --- | --- | --- |
| `pldm-endpoint-agent` | `services/pldm-endpoint-agent/main.cpp` | 綁定 UDS socket，提供 EID 8 GPU 與 EID 9 NIC 的 project-local PLDM response |
| `bmc-sensor-service` | `services/bmc-sensor-service/main.cpp` | 探索 endpoint、每秒輪詢資料、讀取 optional sysfs、計算 threshold、發布 D-Bus objects |
| `redfish-service` | `services/redfish-service/main.cpp` | 監聽 `127.0.0.1:8080`，將 D-Bus snapshot 映射為 HTTP JSON |

`scripts/run_session.sh` 使用 `dbus-run-session` 建立私有 bus，並將
`DBUS_SESSION_BUS_ADDRESS` 寫入 `runtime/dbus-session.env`，供 demo script 與
`dbus_dump` 使用。

## CMake targets

| Target | 原始碼範圍 |
| --- | --- |
| `mini_common` | logger、threshold engine |
| `mini_platform` | PCI sysfs、synthetic PCIe sysfs backend、hwmon backend |
| `mini_protocol` | MCTP packet/reassembly/UDS、PLDM message/PDR/responders |
| `mini_dbus` | sd-bus server/client |
| `mini_redfish` | D-Bus property 與 HTTP JSON 映射 |

若 CMake 找不到 `libsystemd`，`mini_dbus` 仍可編譯 stub，但
`DbusClient::connect()` 會回傳 unavailable，完整 session 無法執行。單純 build
成功不代表 D-Bus runtime 已可用。

## D-Bus 模型

Service name：

```text
xyz.openbmc_project.MiniBMC.SensorService
```

Manager：

```text
Path:      /xyz/openbmc_project/MiniBMC
Interface: xyz.openbmc_project.MiniBMC.Manager
Methods:   ListObjects, GetObject, InjectFault
```

`ListObjects` 回傳 `a{ss}`，value 是 JSON string；`GetObject` 也回傳 JSON
string。這是本專案自訂的管理介面，不是 OpenBMC ObjectMapper API。

Sensor objects 使用下列 interfaces：

```text
xyz.openbmc_project.Sensor.Value
xyz.openbmc_project.Sensor.Threshold.Critical
xyz.openbmc_project.State.Decorator.Health
```

Inventory objects 使用：

```text
xyz.openbmc_project.Inventory.Item
xyz.openbmc_project.State.Decorator.Health
```

Event objects 使用：

```text
xyz.openbmc_project.Logging.Entry
```

`DbusServer::process()` 對 sensor update 發送 `PropertiesChanged`。Inventory
與 event object 建立時會匯出 vtable，但目前不對既有 inventory/event property
update 發送 change signal。

## Sensor object paths

固定 sensor ID 與 path 定義於 `libs/dbus/dbus_names.hpp`：

```text
GPU0_Core_Temp
  /xyz/openbmc_project/sensors/temperature/gpu0_core
GPU0_Power
  /xyz/openbmc_project/sensors/power/gpu0_power
GPU0_PCIe_Correctable_Errors
  /xyz/openbmc_project/sensors/count/gpu0_pcie_correctable_errors
GPU0_PCIe_Link_Status
  /xyz/openbmc_project/sensors/network/gpu0_pcie_link_status
NIC0_Temp
  /xyz/openbmc_project/sensors/temperature/nic0
NIC0_Link_Status
  /xyz/openbmc_project/sensors/network/nic0_link_status
NIC0_Correctable_Errors
  /xyz/openbmc_project/sensors/count/nic0_correctable_errors
NIC0_Packet_Errors
  /xyz/openbmc_project/sensors/count/nic0_packet_errors
Fan0_Tach
  /xyz/openbmc_project/sensors/fan_tach/fan0
CPU_Board_Temp
  /xyz/openbmc_project/sensors/temperature/cpu_board_temp
Board_Voltage
  /xyz/openbmc_project/sensors/voltage/board_voltage
```

## Sensor polling

`SensorManager` 的預設 polling interval 是 1000 ms。每個週期執行：

1. `pollPldm()` 依 EID 與 sensor ID 呼叫 `GetSensorReading`。
2. `pollKernelTelemetry()` 優先讀取 synthetic PCIe module sysfs。
3. 若 synthetic PCIe sysfs 不存在，改掃描 `/sys/bus/pci/devices`，取第一個
   可讀裝置建立 `PCIeDevice0` inventory。
4. 尋找 `name == mini_i2c_hwmon` 的 `hwmonX`。
5. 套用本地 fault state。
6. 執行 threshold evaluation。
7. 更新 D-Bus sensor objects，必要時新增 event object。
8. 寫入 JSONL log。

若 PLDM reading 失敗，對應 sensor 會設為 `Unavailable` / `Unknown`。若 hwmon
不存在，三個 hwmon sensors 也會設為 `Unavailable` / `Unknown`。

## Threshold 與 EventLog

`ThresholdEventEngine` 追蹤每個 sensor 的 active upper/lower fault：

- 正常值跨越 threshold 時建立 assertion event。
- 回到 threshold 加減 hysteresis 的正常區間後建立 recovery event。
- fault 狀態未改變時不重複建立 event。

溫度 sensor hysteresis 是 2.0；其他 sensor 是 0.1。`Fan0_Tach` 的 lower
threshold assertion 為 `Warning`，其他已設定 threshold 的 sensor 為
`Critical`。

Event object path：

```text
/xyz/openbmc_project/logging/entry/{sequence}
```

sequence 從每次程序啟動時的 1 開始。資料沒有寫入 database 或獨立 event
file，因此 service 重啟後會消失。

## MCTP transport

`libs/mctp/mctp_packet.*` 定義本專案的 6-byte packet header，payload MTU 為
64 bytes。`fragmentMessage()` 產生 SOM/EOM、2-bit sequence、message tag 與
tag owner；`Reassembler` 檢查：

- sequence 是否連續
- message tag 與 tag owner 是否一致
- message type 是否一致
- 是否在 timeout 前收到 EOM

`UdsMctpClient` 與 `UdsMctpServer` 使用：

```text
AF_UNIX
SOCK_SEQPACKET
runtime/sockets/mctp_endpoint.sock
```

這個 framing 是本 repository 兩端共用的 local transport，不是 Linux
AF_MCTP socket。

## PLDM 子集

支援的 project-local commands：

Type 0：

```text
SetTID
GetTID
GetPLDMVersion
GetPLDMTypes
GetPLDMCommands
```

Type 2：

```text
SetEventReceiver
PlatformEventMessage
GetSensorReading
GetPDRRepositoryInfo
GetPDR
SetFault (project-specific command 0xf0)
```

`SetEventReceiver` 與 `PlatformEventMessage` 只回傳 completion code；沒有保存
event receiver，也沒有非同步 event transport。

PDR 與 numeric reading 使用本專案自訂 serialization，包括直接編碼 little
endian `double`。它適合 repository 內測試，但不能和外部 PLDM implementation
互通。

## Endpoint 資料

EID 8 / TID 8：

| Sensor ID | 名稱 | 值 |
| --- | --- | --- |
| 1 | GPU Core Temperature | 65 Cel |
| 2 | GPU Power | 250 W |
| 3 | PCIe Correctable Error Count | 0 Count |
| 4 | PCIe Link Status | 1 State |

EID 9 / TID 9：

| Sensor ID | 名稱 | 值 |
| --- | --- | --- |
| 101 | NIC Temperature | 48 Cel |
| 102 | NIC Link Status | 1 State |
| 103 | NIC Correctable Error Count | 0 Count |
| 104 | NIC Packet Error Count | 0 Count |

## PCIe 資料來源

`MiniPcieBackend` 讀取：

```text
/sys/class/mini_bmc_pcie/mini_pcie0/
```

若路徑不存在，`PciSysfsReader` 掃描：

```text
/sys/bus/pci/devices/
```

掃描器讀取 BDF、vendor、device、class、revision、driver symlink、link speed、
link width 與 NUMA node。缺少 optional file 時保留 `nullopt`，不讓整個 scan
失敗。

Kernel Module 另外提供 `/dev/mini_pcie0`、ioctl 與 poll，但目前
`bmc-sensor-service` 不開啟 character device。

## hwmon 資料來源

`HwmonSensorBackend::discover()` 掃描 `/sys/class/hwmon/hwmon*`，讀取 `name`
找到 `mini_i2c_hwmon`，再取得：

```text
temp1_input
temp1_label
in1_input
in1_label
fan1_input
fan1_label
```

檔名中的 `i2c` 是既有 module 名稱；Kernel 實作是自行註冊 platform device，
沒有 I2C adapter、client、regmap 或硬體 transaction。

## HTTP JSON 映射

`SensorController`、`PcieController` 與 `EventLogController` 透過
`DbusClient` 讀取 snapshot。`libs/redfish/redfish_mapper.cpp` 建立帶有
`@odata.type`、`@odata.id`、`Status` 與 `Oem.MiniOpenBMC` 的 JSON。

這些 JSON shape 只涵蓋 demo 需要的欄位。程式沒有載入 Redfish schema，也沒有
執行 conformance validation。

HTTP error mapping：

| Internal status | HTTP status |
| --- | --- |
| invalid argument / malformed data | 400 |
| not found | 404 |
| unavailable / timeout / I/O error | 503 |
| internal error | 500 |

## 結構化紀錄

三個 service 共用：

```text
runtime/logs/mini-openbmc-service.jsonl
```

每行是一個 JSON object，至少包含 `timestamp`、`component`、`level` 與
`message`。多程序各自以 append mode 開啟檔案；目前沒有跨程序 file lock、
rotation 或 retention policy。

`trace_cli` 會讀取 JSONL，依 component/message 做簡單分類，並產生：

```text
runtime/generated_trace.md
```

此輸出是 log 摘要，不代表精確的 call span 或封包時序。

## 驗證層級

| 層級 | 覆蓋範圍 |
| --- | --- |
| GoogleTest / CTest | packet、reassembly、PLDM responder、mapping、threshold、temporary sysfs |
| `demo_mctp_pldm.sh` | endpoint process、UDS socket、MCTP/PLDM request-response |
| `smoke_test.sh` | 三程序、私有 D-Bus、HTTP routes、fault、EventLog |
| Kernel compile | module 是否可針對指定 headers 編譯 |
| Kernel runtime | module load、sysfs、hwmon、character device、ioctl、poll |

Kernel runtime 不包含在一般 CTest 或 smoke test 中，必須在 headers 與 running
kernel 相符且允許載入 module 的 Linux 環境另外執行。
