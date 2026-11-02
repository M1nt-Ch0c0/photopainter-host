# photopainter-host

PhotoPainter 相框的宿主固件（ESP-IDF 工程）。宿主写在 factory 分区，开机即运行，负责所有底层且共用的事：蓝牙、配对与鉴权、应用目录和 A/B 槽、加载与调度应用、屏幕驱动、功能键、Wi-Fi。具体业务（开机首屏、收图、显示什么）由宿主加载的独立应用完成，默认应用是 [photoframe](https://github.com/M1nt-Ch0c0/photoframe)。

这一页讲这个仓库有什么、怎么构建和测试、怎么打包应用、怎么发布宿主固件。整个项目的介绍和原理在 [项目文档](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/README.md)，其中与宿主关系最紧的是：

- [设备是怎么工作的](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/03-how-the-device-works.md)：宿主与应用的分工、生命周期、A/B 槽、启动保护、显示链路
- [应用接口参考](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/05-app-api-reference.md)：应用能调用的宿主服务（ABI 2.0–2.3）
- [蓝牙通信](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/06-bluetooth.md)：配对、会话、管理命令和图片事务的设计
- [维护与排错](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/08-maintenance.md)：备份、USB 刷写宿主、恢复

## 硬件与工具链

| 项目 | 要求 |
|---|---|
| 硬件 | 微雪 ESP32-S3 PhotoPainter，N16R8（16 MiB Flash、8 MiB PSRAM），7.3 英寸 800×480 Spectra 6 六色电子纸 |
| ESP-IDF | 固定提交 `5e6f53cdb31fe5708eae3f55af9737be2822db22`（6.0.3）。`CMakeLists.txt` 会检查，不符时拒绝构建 |
| ELF 加载器 | 组件仓库的 `espressif/elf_loader` 1.3.3，不修改 |
| 其他组件 | `espressif/cjson` 1.7.19 |

## 仓库结构

| 路径 | 内容 |
|---|---|
| `main/` | 宿主源码：蓝牙协议与传输、配对、应用目录与槽位、启动保护、App Manager、宿主服务、Wi-Fi、HTTP 健康检查 |
| `components/photopainter_board/` | 屏幕和电源驱动，把应用的逻辑帧旋转打包后送上电子纸，见 [组件说明](components/photopainter_board/README.md) |
| `sdk/` | 应用 SDK 原件：`photopainter_app.h`（接口和结构体）、`abi2_imports.inc`（允许导入的符号）。photoframe 仓库的 `components/app_sdk/` 是它的副本，两边必须一致 |
| `tools/` | 打包、配网、诊断工具和 Python 测试 |
| `tests/` | 由 Python 测试编译运行的 C 原生测试和桩 |
| `docs/` | 宿主专属的协议和规格，见下文 |
| `partitions.csv` | 分区表 |

## 构建

```sh
. /path/to/esp-idf/export.sh          # 固定提交的 ESP-IDF
idf.py -B build-host -DSDKCONFIG=build-host/sdkconfig build
```

产物是 `build-host/photopainter_host.bin`。用独立的构建目录和 sdkconfig，可以保证 `sdkconfig.defaults` 里的蓝牙等配置都生效。宿主独立构建，不依赖兄弟仓库，也不内嵌任何应用。

## 测试

```sh
python3 -m unittest discover -s tools -p 'test_*.py'
```

测试会用本机的 `cc` 编译宿主的生产 C 代码（协议部分链接 OpenSSL，开启 ASan 和 UBSan），配合 `tests/` 下的平台桩运行。需要本机有 C 编译器和 OpenSSL 开发库。两个环境变量可以打开额外的集成检查：

| 变量 | 作用 |
|---|---|
| `PHOTOFRAME_TEST_ELF=/path/to/app.elf` | 用一个真实的应用 ELF 检查宿主的 ELF 和清单校验 |
| `IDF_PATH` | 调用 ESP-IDF 的 NVS 生成器检查 `provision.py` |

`python3 tools/doctor.py` 检查本机开发环境：Python、Chrome、ESP-IDF 提交、`idf.py`、兄弟仓库和串口。默认不打开串口；加 `--probe-serial --serial 端口` 会打开串口抓启动日志，可能让设备复位。`--json` 输出 JSON，`--strict` 有失败项时返回非零。

## 打包应用

应用在各自的仓库里独立构建成 `.app.elf`，再用本仓库的工具打包成 `.pkg`：

```sh
python3 tools/module.py package --app lifecycle --version 1 \
  --input ../photoframe/examples/lifecycle/build-app/lifecycle.app.elf \
  --output /tmp/lifecycle-v1.pkg
```

包格式、宿主的校验规则和安装后的槽位变化见 [应用目录与槽位](docs/app-catalog.md)。安装、激活、切换应用通过已配对的蓝牙客户端完成，例如 `ai-quota-frame ble install` 和 `ai-quota-frame ble activate`，或网页插件的开发者工具，见 [电脑端、控制台与网页插件](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/07-clients.md)。

`tools/module.py` 的其他动作（`status`、`stage`、`activate` 等）走旧的 HTTP 接口。当前宿主对这些接口一律返回 410，只有 `package` 有用。

## 发布宿主固件

宿主固件只通过 USB 更新，并且只写 factory 分区 `0x10000`。Wi-Fi、配对和已安装的应用都保留。不要对已经在用的相框执行 `idf.py flash`，它会覆盖分区表、NVS 和应用数据。

推荐用 `ai-quota-frame` 仓库的 `deploy/usb-maintenance.py --action usb-flash`：它会核对设备身份、先完整备份 16 MiB Flash、检查分区表，再只写 factory。完整步骤见 [用 USB 更新宿主固件](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/08-maintenance.md#用-usb-更新宿主固件)。

要使用新 ABI 的应用时，先升级宿主，再安装应用。宿主只接受 ABI 次版本不高于自己的应用（当前是 2.3）。

## 网络接口

| 接口 | 用途 |
|---|---|
| 蓝牙管理服务 | 所有控制：配对、状态、安装和管理应用、应用通信。见 [宿主蓝牙线上协议](docs/ble-protocol.md) |
| HTTP `GET /health`（端口 80） | 只用于探测相框是否在线，返回 `{"device":"PhotoPainter","protocol":2,"transport":"paired-ble"}` |
| HTTP `/api/push`、`/api/module`、`/api/wifi` | 一律返回 410，不执行任何操作 |

## 文档

`docs/` 里是这个仓库独有的精确规格，原理和使用说明在项目文档里：

| 文档 | 内容 |
|---|---|
| [宿主蓝牙线上协议](docs/ble-protocol.md) | 广播、管理服务特征、分片写入、签名请求、错误码、op 1–28、状态 JSON |
| [配对 v2 规格](docs/ble-pairing-v2.md) | op 18–23、密钥派生、状态机、主人记录、长按重置 |
| [ABI 2.2 CoC 通道规格](docs/abi-2-2-coc.md) | CoC 的 hello、记录封套、方向密钥、流控 |
| [ABI 2.3 消息通道规格](docs/abi-2-3-channel.md) | op 24–27、大小限制、会话与授权 |
| [应用目录与槽位](docs/app-catalog.md) | 分区地址、安装包格式、目录、管理命令、启动记录、功能键 |
| [Wi-Fi 配置](docs/wifi-config.md) | `config/wifi.json` 格式、SD 与 NVS 优先级、配置工具 |

开发者助手的工作规则在 [AGENTS.md](AGENTS.md)。

## 许可

MIT，见 [LICENSE](LICENSE)。屏幕驱动部分改编自微雪的 PhotoPainter 示例代码，保留其 MIT 声明（`components/photopainter_board/LICENSE.Waveshare.txt`）。
