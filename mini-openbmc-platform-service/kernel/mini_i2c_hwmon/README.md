# mini_i2c_hwmon Kernel Module

`mini_i2c_hwmon` 是合成 hwmon provider。Module 以 platform device 註冊 Linux
hwmon attributes；名稱雖保留 `i2c`，目前沒有 `i2c_client`、I2C adapter、
regmap 或實體 bus transaction。

載入後可依 `name` 找到動態配置的 `hwmonX`：

```bash
name_file=$(grep -l mini_i2c_hwmon /sys/class/hwmon/hwmon*/name | head -1)
hwmon=$(dirname "$name_file")
```

## hwmon attributes

```text
name
temp1_input
temp1_label
in1_input
in1_label
fan1_input
fan1_label
fault_mode
```

正常合成值：

```text
temp1_input=42000
in1_input=12000
fan1_input=8000
```

`fault_mode` 支援：

```text
none
read_timeout
device_disappeared
stuck_value
out_of_range
invalid_reading
```

目前 `stuck_value` 保持正常固定值；module 沒有背景更新來源，因此它與 `none`
在讀值上沒有差異。其他 fault 會回傳 read error 或合成異常值。

## 建置與載入（Build and Load）

從 repository root 執行：

```bash
./scripts/build_kernel_modules.sh
sudo ./scripts/load_kernel_modules.sh
./scripts/demo_i2c_hwmon.sh
sudo ./scripts/unload_kernel_modules.sh
```

Runtime 必須使用與 `uname -r` 相符的 headers。這個 module 不需要實體 I2C
裝置。
