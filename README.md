# bmc_lab

`bmc_lab` 是一個 BMC / OpenBMC 相關的實驗工作區，目前放了兩個子專案：

```text
bmc_lab/
├── README.md
├── mini-openbmc-platform-service/
└── openbmc-ai-server-management-stack/
```

這裡的程式都以一般 Linux 開發環境為主，方便在 Ubuntu / apt 環境下建置、執行與觀察。  
本 repository 不包含完整 OpenBMC distribution、Yocto image、AST2600/AST2700 韌體映像，也不會直接控制真實 BMC 硬體。

目前範圍比較接近一組可執行的 BMC 實驗專案：把服務邊界、D-Bus、Redfish-style API、感測器資料、事件流程、故障注入與部分 telemetry provider 拆開實作，讓每一段資料怎麼來、怎麼被處理、最後怎麼對外呈現都能被追蹤。

---

## 子專案

| 子專案 | 內容 |
| --- | --- |
| `openbmc-ai-server-management-stack` | 以 AI 伺服器為對象的管理堆疊原型。核心是 `HardwareModel` 與一組 management services，負責處理散熱、功耗、故障事件與韌體更新流程，並透過 Redfish-style HTTP API 與 D-Bus 對外提供狀態。 |
| `mini-openbmc-platform-service` | 比較偏平台服務與協定路徑。專案由多個 userspace service 組成，使用 private D-Bus session 串接感測器服務與 HTTP API，並用 UDS 模擬 MCTP / PLDM endpoint。另有選用的 synthetic PCIe telemetry 與 hwmon kernel module。 |

兩個專案處理的層級不同：

- `openbmc-ai-server-management-stack` 把重點放在管理邏輯本身，例如平台狀態、策略判斷、事件記錄與 API 回應。
- `mini-openbmc-platform-service` 把重點放在服務拆分與資料路徑，例如 service IPC、sensor publishing、UDS-MCTP/PLDM，以及 telemetry provider。

---

## 環境需求

建議環境：

- Ubuntu 22.04 / 24.04，或相近的 apt-based Linux 發行版
- Bash
- CMake
- Ninja 或 Make
- 支援 C++20 的 GCC / Clang
- `dbus` / `libsystemd-dev`
- `curl`
- `jq`

只執行 userspace demo 時，不需要 OpenBMC image，也不需要真實 BMC 板子。

如果要執行 `mini-openbmc-platform-service` 的 kernel module demo，環境必須能載入自訂 `.ko`，而且 module vermagic 要和 `uname -r` 相同。一般 WSL kernel 通常不適合拿來跑這段；可以先只跑 userspace build、test 與 smoke test。

---

## 快速開始

以下指令假設目前位於父專案目錄：

```bash
cd bmc_lab
```

### openbmc-ai-server-management-stack

```bash
cd openbmc-ai-server-management-stack

chmod +x scripts/*.sh
./scripts/01_install_deps.sh
./scripts/02_build.sh
```

前景執行：

```bash
./scripts/03_run.sh
```

另一個 terminal 可執行：

```bash
./scripts/04_demo_redfish.sh
./scripts/05_demo_fault_injection.sh
```

常用檢查：

```bash
curl -s http://127.0.0.1:8080/redfish/v1 | jq
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Thermal | jq
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/chassis/Power | jq
busctl --user tree xyz.openbmc_project.AIServer
```

清理：

```bash
./scripts/99_cleanup.sh
./scripts/98_distclean.sh
```

回到父目錄：

```bash
cd ..
```

---

### mini-openbmc-platform-service

```bash
cd mini-openbmc-platform-service

chmod +x scripts/*.sh
./scripts/install_deps.sh
./scripts/clean.sh
./scripts/build.sh
./scripts/test.sh
```

完整 smoke test：

```bash
./scripts/smoke_test.sh
```

手動啟動完整 session：

```bash
./scripts/run_session.sh
```

另一個 terminal 可執行：

```bash
./scripts/demo_redfish_sensors.sh
./scripts/demo_dbus_objects.sh
./scripts/demo_fault_injection.sh
```

常用檢查：

```bash
curl -s http://127.0.0.1:8080/redfish/v1 | jq
curl -s http://127.0.0.1:8080/redfish/v1/Chassis/GPU0/Sensors | jq

source runtime/dbus-session.env
build/tools/dbus_dump
build/tools/trace_cli --flow all
```

選用 kernel module demo：

```bash
./scripts/build_kernel_modules.sh
sudo ./scripts/load_kernel_modules.sh
./scripts/demo_pcie_kernel_telemetry.sh
./scripts/demo_i2c_hwmon.sh
sudo ./scripts/unload_kernel_modules.sh
```

清理：

```bash
./scripts/clean.sh
```

回到父目錄：

```bash
cd ..
```

---

## 執行注意事項

兩個子專案預設都使用 `8080`，建議一次只跑一個，避免 port collision。

`mini-openbmc-platform-service` 的 HTTP server 固定監聽 `127.0.0.1:8080`。  
`openbmc-ai-server-management-stack` 可以用參數指定 port，但預設 scripts 仍以 `8080` 為主。

如果遇到 D-Bus 權限警告，先看服務是否已經 fallback 到 user bus。只要後續服務成功啟動，通常不需要特別處理 system bus 權限。

---

## 專案邊界

這個工作區目前不宣告支援：

- 完整 OpenBMC distribution
- Yocto recipe 或可燒錄 BMC image
- AST2600 / AST2700 真實板級控制
- Redfish conformance
- 真實 MCTP controller 或外部 PLDM endpoint interoperability
- 真實 PCIe BAR、DMA、AER 或 I2C transaction
- 生產環境用的認證、授權、TLS 與帳號管理

這些限制是刻意保留的邊界。這個 repository 目前重點是讓 BMC 類型系統中的幾條常見資料路徑可以被建置、執行、觀察與修改，而不是包成完整產品。

---

## 提交前檢查

在父專案提交前，可以先確認沒有 build/runtime/run 之類的產物被追蹤：

```bash
git status --short
```

兩個子專案各自的基本檢查如下。

```bash
# openbmc-ai-server-management-stack
cd openbmc-ai-server-management-stack
./scripts/02_build.sh
./scripts/99_cleanup.sh
./scripts/98_distclean.sh
cd ..

# mini-openbmc-platform-service
cd mini-openbmc-platform-service
./scripts/build.sh
./scripts/test.sh
./scripts/clean.sh
cd ..
```

如果只改其中一個子專案，至少跑該子專案自己的 build / test script。

---

## 文件入口

```text
openbmc-ai-server-management-stack/
├── README.md
├── report_bmc_api.md
└── docs/

mini-openbmc-platform-service/
├── README.md
├── docs/
└── kernel/
    ├── mini_pcie_telemetry/README.md
    └── mini_i2c_hwmon/README.md
```
