# mini_pcie_telemetry Kernel Module

`mini_pcie_telemetry` 是合成 PCIe telemetry provider。Module 載入後建立：

```text
/dev/mini_pcie0
/sys/class/mini_bmc_pcie/mini_pcie0/
```

它不註冊 PCI driver，不搜尋 PCI device，也不存取 BAR、DMA、AER 或實體 GPU。
所有 telemetry 都保存在 module 記憶體中，供 sysfs、character device 與
ioctl 測試。

## sysfs attributes

```text
device_id
link_width
link_speed
link_state
gpu_core_temp_millic
gpu_power_milliwatt
correctable_error_count
nonfatal_error_count
health
fault_mode
```

`fault_mode` 支援：

```text
none
link_down
link_degraded
correctable_error_spike
nonfatal_error
telemetry_timeout
over_temperature
over_power
```

寫入 fault 會更新 synthetic telemetry、增加 event sequence 並喚醒 poll wait
queue。`correctable_error_spike` 期間，timer 每秒再增加 error count。

## Character device

`read()` 回傳單行文字：

```text
device=MiniGPU-0000 temp_millic=65000 power_mw=250000 link_width=16 link_speed=16.0GT/s state=L0 health=OK
```

ioctl 定義於 `mini_pcie_telemetry.h`：

```text
MINI_PCIE_GET_TELEMETRY
MINI_PCIE_SET_FAULT
MINI_PCIE_CLEAR_FAULT
MINI_PCIE_INJECT_EVENT
```

`poll()` 在 event sequence 改變時回傳 readable/priority event。每個 open file
descriptor 保存自己的 sequence snapshot。

目前 `bmc-sensor-service` 只讀取 sysfs，不使用 character device、ioctl 或
poll。

## 建置與載入（Build and Load）

從 repository root 執行：

```bash
./scripts/build_kernel_modules.sh
sudo ./scripts/load_kernel_modules.sh
./scripts/demo_pcie_kernel_telemetry.sh
sudo ./scripts/unload_kernel_modules.sh
```

Runtime 必須使用與 `uname -r` 相符的 headers。若 build script 顯示
compile-only warning，產生的 `.ko` 不可載入。
