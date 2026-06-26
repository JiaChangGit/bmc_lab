# 問題排查（Troubleshooting）

先從專案根目錄執行：

```bash
./scripts/clean.sh
./scripts/build.sh
./scripts/test.sh
bash -n scripts/*.sh
```

若問題只出現在完整 session，再執行：

```bash
./scripts/smoke_test.sh
```

`runtime/smoke-test/session.log` 通常是第一個需要檢查的檔案。

## CMake configure 失敗

### 找不到 compiler 或 CMake

```bash
cmake --version
c++ --version
```

Ubuntu 可執行：

```bash
./scripts/install_deps.sh
```

### FetchContent 下載失敗

CMake 在系統未安裝對應套件時，會下載：

- nlohmann/json v3.11.3
- cpp-httplib v0.16.3
- GoogleTest v1.14.0

先確認網路與 proxy 設定，或安裝系統套件：

```bash
sudo apt-get install -y \
  nlohmann-json3-dev \
  libcpp-httplib-dev \
  libgtest-dev
```

清除不完整 cache 後重建：

```bash
rm -rf build
./scripts/build.sh
```

### 顯示 D-Bus stub warning

訊息：

```text
libsystemd development files not found; building D-Bus stubs.
```

修正：

```bash
sudo apt-get install -y libsystemd-dev pkg-config
rm -rf build
./scripts/build.sh
```

只重新執行 link 不足以更新 configure 結果，必須重新產生 build directory。

## CTest 失敗

```bash
ctest --test-dir build --output-on-failure
```

單獨執行失敗 binary：

```bash
build/test/test_mctp_fragmentation
build/test/test_threshold_event_engine
```

測試會在 temporary directory 建立 sysfs fixture，不應依賴 host 的
`/sys/class/hwmon` 或 `/sys/bus/pci/devices`。若 fixture 測試失敗，先檢查
測試建立的必要檔案，而不是載入 Kernel Module。

## `run_session.sh` 無法啟動

### `dbus-run-session` 不存在

```bash
sudo apt-get install -y dbus
```

### PLDM endpoint socket 沒有出現

訊息：

```text
PLDM endpoint socket did not appear.
```

檢查：

```bash
ls -la runtime/sockets
build/services/pldm-endpoint-agent/pldm-endpoint-agent
```

常見原因：

- 另一個 `run_session.sh` 或 `demo_mctp_pldm.sh` 正在使用相同 socket。
- 舊程序仍在執行。
- endpoint binary 不存在或沒有 execute permission。

不要同時執行 `demo_mctp_pldm.sh` 與完整 session；兩者使用相同的
`runtime/sockets/mctp_endpoint.sock`。

### HTTP service 沒有 ready

檢查：

```bash
cat runtime/smoke-test/session.log
curl -v http://127.0.0.1:8080/redfish/v1
```

若 8080 已被其他程序占用：

```bash
ss -ltnp | grep ':8080'
```

目前 server address/port 固定在程式中，沒有 runtime option 可改 port。

### D-Bus service unavailable

確認 build 時沒有使用 stub，並在完整 session 外部工具連線前載入：

```bash
source runtime/dbus-session.env
build/tools/dbus_dump
```

`runtime/dbus-session.env` 只在 `run_session.sh` 執行期間有效。

## HTTP 回傳 404、400 或 503

### Sensor 404

Sensor ID 必須和 `libs/dbus/dbus_names.hpp` 完全一致，例如：

```text
GPU0_Core_Temp
NIC0_Temp
Fan0_Tach
```

### Fault route 回傳 400

Request body 必須包含：

```json
{
  "target": "GPU0_Core_Temp",
  "fault": "out_of_range",
  "enabled": true
}
```

未知 target、未知 fault 或無效 JSON 都會回傳 400。

### Route 回傳 503

503 表示 D-Bus unavailable、transport timeout 或 I/O error。先檢查：

```bash
source runtime/dbus-session.env
build/tools/dbus_dump
tail -n 50 runtime/logs/mini-openbmc-service.jsonl | jq .
```

hwmon module 未載入時，三個 hwmon sensor 應回傳 HTTP 200，但
`Status.State` 是 `Unavailable`；這本身不是 503。

## MCTP / PLDM 問題

獨立驗證：

```bash
./scripts/demo_mctp_pldm.sh
```

如果顯示 connection refused 或 socket unavailable：

```bash
rm -f runtime/sockets/mctp_endpoint.sock
./scripts/demo_mctp_pldm.sh
```

不要在 endpoint process 仍執行時刪除 socket。先終止舊程序，再移除 stale
socket。

可用測試確認 fragmentation 與 timeout handling：

```bash
build/test/test_mctp_packet
build/test/test_mctp_fragmentation
build/test/test_pldm_type0
build/test/test_pldm_type2
```

## Sensor 值沒有更新

`SensorManager` 預設每秒輪詢一次。先等待一個 polling cycle，再查詢：

```bash
sleep 2
curl --fail --silent \
  http://127.0.0.1:8080/redfish/v1/Chassis/GPU0/Sensors/GPU0_Core_Temp |
  jq .
```

檢查 PLDM log：

```bash
build/tools/trace_cli --flow pldm
build/tools/trace_cli --flow mctp
```

若 endpoint 無法回覆，PLDM sensor 會變成 `Unavailable`，不會保留最後成功值。

## Fault 注入後沒有 EventLog

確認 fault 已接受：

```bash
curl --fail --silent -X POST \
  http://127.0.0.1:8080/debug/faults \
  -H 'Content-Type: application/json' \
  -d '{"target":"GPU0_Core_Temp","fault":"out_of_range","enabled":true}' |
  jq .
```

等待 polling：

```bash
sleep 2
curl --fail --silent \
  http://127.0.0.1:8080/redfish/v1/Systems/System0/LogServices/EventLog/Entries |
  jq .
```

Event 只在 threshold state 改變時建立；fault 持續期間不會每秒重複新增。

## PCIe inventory 不存在

未載入 synthetic module 時，service 會掃描：

```text
/sys/bus/pci/devices
```

檢查：

```bash
build/tools/pci_scan
```

若該目錄不存在、不可讀或沒有裝置，`PCIeDevice0` 不會發布。這也會讓
`smoke_test.sh` 的 D-Bus object count 檢查失敗。

## Kernel Module build 失敗

確認 running kernel 與 headers：

```bash
uname -r
ls -ld "/lib/modules/$(uname -r)/build"
```

可指定 headers tree：

```bash
KDIR=/path/to/kernel/build ./scripts/build_kernel_modules.sh
```

`KDIR` 必須包含可供 external module build 使用的 Makefile 與 generated
headers。

## Module 無法載入

比較版本：

```bash
uname -r
modinfo -F vermagic \
  kernel/mini_pcie_telemetry/mini_pcie_telemetry.ko
modinfo -F vermagic \
  kernel/mini_i2c_hwmon/mini_i2c_hwmon.ko
```

第一個欄位必須與 `uname -r` 相同。`load_kernel_modules.sh` 會在版本不符時
停止，不應使用 `insmod --force` 繞過。

其他常見限制：

- Secure Boot 或 module signing policy
- container / WSL 不允許載入 module
- 缺少 `CAP_SYS_MODULE`
- module 已由其他測試載入

## PCIe module runtime

載入後檢查：

```bash
ls -l /dev/mini_pcie0
find /sys/class/mini_bmc_pcie/mini_pcie0 -maxdepth 1 -type f
cat /dev/mini_pcie0
```

`fault_mode` 只接受：

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

其他值會回傳 `Invalid argument`。

## hwmon module runtime

`hwmonX` 編號由 Kernel 動態配置，不要假設是 `hwmon0`：

```bash
name_file=$(grep -l mini_i2c_hwmon /sys/class/hwmon/hwmon*/name | head -1)
hwmon=$(dirname "$name_file")
cat "$hwmon/temp1_input"
cat "$hwmon/in1_input"
cat "$hwmon/fan1_input"
```

`read_timeout` 會讓 read 回傳 timeout error，`device_disappeared` 回傳
`ENODEV`，`out_of_range` 與 `invalid_reading` 會回傳合成異常值。

## JSONL log 問題

確認 path 與格式：

```bash
ls -l runtime/logs/mini-openbmc-service.jsonl
jq -c . runtime/logs/mini-openbmc-service.jsonl >/dev/null
```

所有 service 會 append 到同一個檔案，目前沒有 rotation。需要乾淨 log 時先
停止 session，再執行：

```bash
rm -f runtime/logs/mini-openbmc-service.jsonl
```

`trace_cli` 遇到 malformed line 會跳過該行並輸出：

```text
Skipping malformed JSONL record
```

## 完整回歸檢查

```bash
./scripts/clean.sh
./scripts/build.sh
./scripts/test.sh
./scripts/demo_mctp_pldm.sh
./scripts/smoke_test.sh
./scripts/build_kernel_modules.sh
bash -n scripts/*.sh
```

`demo_mctp_pldm.sh` 與 `smoke_test.sh` 必須序列執行，不能並行使用同一個 UDS
socket。
