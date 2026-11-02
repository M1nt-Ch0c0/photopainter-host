# Wi-Fi 配置

这一页是宿主读取和保存 Wi-Fi 配置的精确规则：SD 卡上 `config/wifi.json` 的格式和校验、SD 与 NVS 的优先级、首次迁移、文件的原子写入，以及配套工具 `tools/wifi_profiles.py` 和 `tools/provision.py` 的用法。

推图和应用管理都走蓝牙，相框不连 Wi-Fi 也能正常使用。日常怎样改 Wi-Fi、没有 IP 时怎么排查，见 [维护与排错](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/08-maintenance.md#修改-wi-fi)。应用拿不到 Wi-Fi 凭据，只能从事件上下文里看到“是否已连接”。

## 配置文件格式

SD 卡上的 `config/wifi.json`：

```json
{"version":1,"networks":[
  {"ssid":"Home-2.4G","password":"replace-home-password"},
  {"ssid":"Office-2.4G","password":"replace-office-password"}
]}
```

仓库里的 `wifi.example.json` 是同样的示例。

| 规则 | 值 |
|---|---|
| `version` | 必须为 1 |
| 网络数 | 0–10 个，数组顺序就是优先级。空数组是合法的“明确不配网络” |
| `ssid` | 1–32 字节 UTF-8，不含控制字符，列表里不能重复 |
| `password` | 空（开放网络）、8–63 个可打印 ASCII 字符，或 64 位十六进制 PSK |
| 文件 | 不超过 8192 字节，嵌套不超过 4 层，不含 NUL，同一对象里不能有重复键 |

ESP32-S3 只支持 2.4 GHz 网络。

## 读取顺序

宿主开机时按下面的顺序决定用哪份配置：

1. 读 NVS（`nvs` 分区命名空间 `photo`）：先读键 `wifi_profiles`（多网络列表）；没有这个键时，读旧的单网络键 `wifi_ssid` 和 `wifi_pass`。
2. 挂载 SD 卡，读 `config/wifi.json`。主文件不存在时读 `config/wifi.json.bak`。
3. SD 上的文件存在且合法，就用它（包括空列表）。
4. SD 上两个文件都不存在时，做一次**首次迁移**：有合法的 NVS 配置就把它写成 `config/wifi.json`；否则读卡根目录的旧 `wifi.txt`（第一行 SSID，第二行密码，支持 CRLF 和空密码），写成 `config/wifi.json`。
5. 没有卡（初始化命令超时）时，用 NVS 里的配置。

| 情况 | 结果 |
|---|---|
| SD 主文件损坏 | 报错，不用 `.bak` 或 NVS 顶替，不覆盖坏文件 |
| NVS 配置损坏 | 不做首次迁移；SD 上有合法文件时仍用 SD |
| SD 挂载失败（不是“没有卡”） | 报错，不配网，SD 上的数据保持原样 |
| 拔掉卡 | 下次开机用 NVS 里的配置。SD 上的配置不会反向写回 NVS |

Wi-Fi 配置有问题不会阻止应用启动。

SD 卡只在开机读取或迁移时挂载，用完立即卸载。宿主不格式化卡，也不访问卡上的其他文件。SD 用 SDMMC 4 线模式，引脚 CLK 39、CMD 41、D0 40、D1 1、D2 2、D3 38，与屏幕的 SPI 总线分开。板上没有插卡检测脚，宿主把初始化超时当作没有卡。

## 原子写入

宿主（首次迁移时）和 `tools/wifi_profiles.py` 用同样的方式写文件：

1. 把完整内容写到 `config/wifi.json.tmp`，刷新并 `fsync`；
2. 主文件存在时，把它改名为 `config/wifi.json.bak`；
3. 把 `.tmp` 改名为主文件；
4. 删除 `.bak`。

任何一步中断，开机时要么读到完整的新文件，要么因为主文件不存在而读到 `.bak`。宿主不会读 `.tmp`。

## 连接行为

- 一个常驻任务按列表顺序逐个尝试，每个网络最多等 20 秒拿到连接；明确断线或连接错误时立即试下一个。
- 连上后一直保持；断线后从第一个网络重新开始。
- 全部失败后等 5 秒，再从头试。连不上不会删除任何配置。
- Wi-Fi 省电关闭（`WIFI_PS_NONE`）。
- 连接状态和 IP 会同步到蓝牙发现记录里（见 [广播与发现记录](ble-protocol.md#广播与发现记录)），并通过 NETWORK 事件通知前台应用。

## 编辑 SD 上的配置

关机取卡，用读卡器接到电脑上，用 `tools/wifi_profiles.py` 编辑，再插回相框开机。这种方式保留配对关系。

```sh
# private-network.env 里写 WIFI_SSID=... 和 WIFI_PASSWORD=...
python3 tools/wifi_profiles.py upsert --file /Volumes/SD/config/wifi.json --config private-network.env
python3 tools/wifi_profiles.py move   --file /Volumes/SD/config/wifi.json --index 2 --to 1
python3 tools/wifi_profiles.py remove --file /Volumes/SD/config/wifi.json --index 2
python3 tools/wifi_profiles.py status --file /Volumes/SD/config/wifi.json
```

| 动作 | 作用 |
|---|---|
| `upsert` | 同名 SSID 更新密码；否则追加到末尾。凭据从 `--config` 指定的私有文件读取，不出现在命令行和输出里 |
| `move` | 把第 `--index` 个网络移到第 `--to` 位 |
| `remove` | 删除第 `--index` 个网络。删光后保存一个空列表 |
| `status` | 只读，不修改文件 |

每个动作输出一行 JSON：`operation`、`source`（读的是主文件 `main`、备份 `backup`，还是文件不存在 `missing`）和网络数 `networks`，不输出 SSID 和密码。序号从 1 开始。文件损坏或参数不对时工具报错退出，不修改文件。

## 写入 NVS

没有 SD 卡时，可以把配置写进 NVS。`tools/provision.py` 生成整块 NVS 分区镜像（`0x9000`，`0x6000` 字节），需要固定版本的 ESP-IDF 环境。

```sh
# 只生成镜像，不接触设备
python3 tools/provision.py --config private.env --wifi-json private-wifi.json \
  --generate-only config.nvs.bin
```

- `--wifi-json` 给出多网络列表，格式同上；不给时用 `--config` 文件里的 `WIFI_SSID`、`WIFI_PASSWORD` 写单网络。
- 默认不写任何推图令牌。`--legacy-token` 只用于回到旧 v1 固件。
- 生成的文件权限为 0600。

**整块写入 NVS 会替换掉配对主人记录**，写入后要重新配对。写入设备推荐用 `ai-quota-frame` 仓库的 `deploy/usb-maintenance.py --action usb-provision`，它会先完整备份 Flash，见 [维护与排错](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/08-maintenance.md#usb-maintenancepy)。`provision.py --port` 也能直接写入，但不做备份。

## 测试

| 测试 | 覆盖 |
|---|---|
| `tools/test_wifi_profiles.py` | 固件的 JSON 解析、首次迁移、原子写入，以及 Python 侧的 NVS 编码 |
| `tools/test_wifi_worker.py` | 固件连接任务的尝试顺序、超时和重试 |
| `tools/test_wifi_profile_tool.py` | `wifi_profiles.py` 的各个动作 |
| `tools/test_provision.py` | NVS 镜像生成；设置 `IDF_PATH` 时调用 ESP-IDF 的 NVS 生成器做集成检查 |
