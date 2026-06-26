# 架構與流程圖（Architecture and Flow Diagrams）

本文件只描述目前程式碼中可執行的元件與資料路徑。虛線表示選用的 Kernel
Module；未載入 module 時，使用者空間服務仍可透過 PLDM endpoint 與 host PCI
sysfs 執行。

## 系統架構圖（System Architecture Diagram）

```mermaid
flowchart TB
    subgraph Clients["Client processes"]
        Curl["curl / demo scripts"]
        Smoke["smoke_test.sh"]
        Tools["dbus_dump / trace_cli / pci_scan"]
    end

    subgraph UserSpace["Userspace services"]
        Redfish["redfish-service<br/>cpp-httplib"]
        Sensor["bmc-sensor-service<br/>SensorManager"]
        Endpoint["pldm-endpoint-agent<br/>EID 8 and EID 9"]
    end

    subgraph IPC["Local IPC"]
        DBus["Private D-Bus session<br/>sd-bus"]
        UDS["runtime/sockets/mctp_endpoint.sock<br/>SOCK_SEQPACKET"]
    end

    subgraph HostInterfaces["Host and optional kernel interfaces"]
        PCI["/sys/bus/pci/devices"]
        PcieSysfs["/sys/class/mini_bmc_pcie/mini_pcie0"]
        CharDev["/dev/mini_pcie0"]
        Hwmon["/sys/class/hwmon/hwmonX"]
        PcieModule["mini_pcie_telemetry.ko"]
        HwmonModule["mini_i2c_hwmon.ko"]
    end

    Curl --> Redfish
    Smoke --> Redfish
    Tools --> DBus
    Tools --> PCI
    Redfish --> DBus
    Sensor --> DBus
    Sensor <--> UDS
    Endpoint <--> UDS
    PCI --> Sensor
    PcieSysfs --> Sensor
    Hwmon --> Sensor
    PcieModule -.-> PcieSysfs
    PcieModule -.-> CharDev
    HwmonModule -.-> Hwmon
```

對應檔案：

- HTTP 與 routes：`services/redfish-service/`
- D-Bus server/client：`libs/dbus/`
- 感測器聚合：`services/bmc-sensor-service/sensor_manager.cpp`
- UDS transport：`libs/mctp/uds_mctp_transport.cpp`
- PLDM endpoint：`services/pldm-endpoint-agent/`
- sysfs backends：`libs/pcie/`、`libs/hwmon/`
- Kernel providers：`kernel/mini_pcie_telemetry/`、`kernel/mini_i2c_hwmon/`

`redfish-service` 與 `bmc-sensor-service` 只在私有 Session Bus 上互通，沒有
連接 system bus 或現有 OpenBMC service。

## 資料流圖（Data Flow Diagram）

```mermaid
flowchart LR
    Gpu["GPU endpoint data<br/>EID 8"]
    Nic["NIC endpoint data<br/>EID 9"]
    PDR["Project-local PDR records"]
    MCTP["MCTP packet fragmentation<br/>and reassembly"]
    Poll["SensorManager<br/>1 second polling"]
    Kernel["Optional sysfs readings"]
    Threshold["ThresholdEventEngine"]
    SensorObjects["D-Bus sensor objects"]
    EventObjects["D-Bus event objects"]
    Mapper["Redfish mapper"]
    SensorJson["Sensor JSON"]
    EventJson["EventLog JSON"]
    Log["JSONL log"]

    Gpu --> PDR
    Nic --> PDR
    PDR --> MCTP --> Poll
    Kernel --> Poll
    Poll --> Threshold
    Poll --> SensorObjects
    Threshold --> EventObjects
    SensorObjects --> Mapper --> SensorJson
    EventObjects --> Mapper --> EventJson
    Poll --> Log
```

PLDM readings 由 `SensorManager::pollPldm()` 取得；optional sysfs readings 由
`SensorManager::pollKernelTelemetry()` 取得。每次 polling 後，
`publishSensor()` 更新 D-Bus properties。門檻跨越時，
`ThresholdEventEngine` 建立 assertion 或 recovery event，再由
`publishEvent()` 匯出 D-Bus logging object。

## 啟動時序圖（Startup Sequence Diagram）

```mermaid
sequenceDiagram
    participant User
    participant Script as run_session.sh
    participant Bus as dbus-run-session
    participant Endpoint as pldm-endpoint-agent
    participant Sensor as bmc-sensor-service
    participant Redfish as redfish-service

    User->>Script: execute
    Script->>Bus: create private session
    Bus->>Endpoint: start process
    Endpoint->>Endpoint: bind UDS socket
    Script->>Sensor: start after socket appears
    Sensor->>Bus: request MiniBMC service name
    Sensor->>Endpoint: GetTID / GetPLDMTypes / GetPDR
    Endpoint-->>Sensor: project-local PLDM responses
    Sensor->>Bus: export sensor and inventory objects
    Script->>Redfish: start process
    Redfish->>Bus: connect D-Bus client
    Script->>Redfish: poll sensor collection route
    Redfish-->>Script: HTTP 200
    Script-->>User: Demo is ready
```

對應 `scripts/run_session.sh`、`services/*/main.cpp` 與
`SensorManager::discoverPldm()`。腳本只檢查 endpoint socket 與 HTTP sensor
collection 是否 ready，沒有 systemd readiness protocol。

## HTTP 查詢時序圖（Request Sequence Diagram）

```mermaid
sequenceDiagram
    participant Client
    participant HTTP as redfish-service
    participant DBusClient as DbusClient
    participant Bus as Private D-Bus
    participant DBusServer as DbusServer

    Client->>HTTP: GET /redfish/v1/Chassis/GPU0/Sensors/{id}
    HTTP->>DBusClient: getObject(object path)
    DBusClient->>Bus: Manager.GetObject(path)
    Bus->>DBusServer: dispatch method
    DBusServer-->>Bus: JSON string
    Bus-->>DBusClient: method reply
    DBusClient-->>HTTP: parsed JSON properties
    HTTP->>HTTP: sensorPropertiesToRedfish()
    HTTP-->>Client: HTTP 200 JSON
```

這個 route 不直接呼叫 PLDM。HTTP 回應使用最近一次 polling 已發布到 D-Bus
的 snapshot。

## 故障注入時序圖（Fault Injection Sequence Diagram）

```mermaid
sequenceDiagram
    participant Client
    participant HTTP as redfish-service
    participant Bus as Private D-Bus
    participant Sensor as SensorManager
    participant Endpoint as PLDM endpoint
    participant Engine as ThresholdEventEngine

    Client->>HTTP: POST /debug/faults
    HTTP->>Bus: InjectFault(target, fault, enabled)
    Bus->>Sensor: injectFault()
    alt PLDM or transport fault
        Sensor->>Endpoint: project-local SetFault command
        Endpoint-->>Sensor: completion code
    else local threshold-only fault
        Sensor->>Sensor: update injectedFaults map
    end
    Sensor-->>Bus: accepted
    Bus-->>HTTP: true
    HTTP-->>Client: HTTP 200
    Sensor->>Engine: evaluate next polling snapshot
    Engine-->>Sensor: health change and optional event
    Sensor->>Bus: update sensor and event objects
```

`POST /debug/faults` 是測試用途的自訂 route。`out_of_range` 可以直接改變
本地 sensor snapshot；PLDM transport faults 會透過 endpoint 的
`setFault` command 更新模擬行為。

## 模組關係圖（Module Diagram）

```mermaid
classDiagram
    class RestServer
    class SensorController
    class PcieController
    class EventLogController
    class DbusClient
    class DbusServer
    class SensorServiceApp
    class SensorManager
    class ThresholdEventEngine
    class UdsMctpClient
    class UdsMctpServer
    class EndpointAgent
    class Type0Responder
    class Type2Responder
    class MiniPcieBackend
    class PciSysfsReader
    class HwmonSensorBackend
    class RedfishMapper

    RestServer *-- SensorController
    RestServer *-- PcieController
    RestServer *-- EventLogController
    SensorController --> DbusClient
    PcieController --> DbusClient
    EventLogController --> DbusClient
    SensorServiceApp *-- DbusServer
    SensorServiceApp *-- SensorManager
    SensorManager --> DbusServer
    SensorManager *-- ThresholdEventEngine
    SensorManager *-- UdsMctpClient
    SensorManager --> MiniPcieBackend
    SensorManager --> PciSysfsReader
    SensorManager --> HwmonSensorBackend
    SensorManager --> RedfishMapper
    EndpointAgent *-- UdsMctpServer
    EndpointAgent *-- Type0Responder
    EndpointAgent *-- Type2Responder
```

這張圖對應 CMake targets `mini_dbus`、`mini_platform`、`mini_protocol`、
`mini_redfish` 與三個 service executable。`RedfishMapper` 名稱代表 JSON
shape mapping，不是獨立程序。

## Kernel telemetry 關係圖

```mermaid
flowchart LR
    PcieState["Synthetic PCIe state"]
    PcieSysfs["mini_bmc_pcie sysfs"]
    Device["/dev/mini_pcie0"]
    Ioctl["ioctl"]
    Poll["poll wait queue"]
    PcieBackend["MiniPcieBackend"]
    HwmonState["Synthetic temperature / voltage / fan"]
    HwmonCore["Linux hwmon core"]
    HwmonBackend["HwmonSensorBackend"]

    PcieState --> PcieSysfs --> PcieBackend
    PcieState --> Device
    Device --> Ioctl
    Device --> Poll
    HwmonState --> HwmonCore --> HwmonBackend
```

`bmc-sensor-service` 目前只使用 PCIe sysfs 與 hwmon sysfs。Character device、
ioctl 與 poll 由 Kernel Module 提供，但沒有被 service runtime 呼叫；其 ABI
由 header 與單元測試檢查，runtime 行為需在可載入 module 的 Linux 環境驗證。
