# PhotoPainter Host 应用框架

当前源码提供 **物理配对 v2 与 ABI 2.2 单所有权 L2CAP CoC**。配对 v2 已完成离线测试和构建，**尚未实机验证**；按键批准、重启保持绑定、重置换绑和新固件上的物理推图留待现场验收。2026-10-27 的 photoframe v20 CoC 实测属于旧认证固件，不能替代本轮验证，接口见 [ABI 2.2 CoC](docs/abi-2-2-coc.md)。

**源码基线：ABI 2.2 功能提交 `f742a47` 已推送 main。** 控制台现为独立 photopainter-console 仓库；源码提交、部署和验证范围分别记录。实机验收范围见 [代码与证据](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Status-and-Evidence.md)。

本仓库是 ESP32-S3 PhotoPainter 的常驻框架：负责设备通信、鉴权、独立应用安装、ELF 加载、统一调度、切换与宿主服务。业务页面、图片解码和应用自身协议属于 APP。

**物理配对 v2：**设备在网页发起配对后显示六位码，核对一致并短按 GPIO 4 功能键确认；绑定关系持久保存，后续连接自动证明已保存的关系。功能键长按 8 秒并松开可清除绑定。BOOT（GPIO 0）和电源键（GPIO 5）不参与此操作。配对是宿主系统界面，即使业务 APP 故障空闲也能使用；不更改 Wi-Fi、应用目录或显示 ABI。精确协议见 [配对 v2](docs/ble-pairing-v2.md)，验证范围见 [配对验证](docs/validation-pairing-v2-2026-10-29.md)。

**开发从 [PhotoPainter 开发者 Wiki](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Home.md) 开始。** AI 协作先读 [AGENTS.md](AGENTS.md) 和项目 [Skill](.agents/skills/develop-photopainter-stack/SKILL.md)。

## 框架能力

| 部分 | 已提供的能力 |
|---|---|
| 通信与鉴权 | Wi-Fi 配置/连接、常驻 NimBLE、设备发现、物理配对与持久 owner、逐连接双向 HMAC |
| 应用管理 | 五个 APP、各自 A/B、备用槽上传、显式试运行、确认、回滚和有界启动恢复 |
| 运行层 | 单 ELF 前台运行，ABI 1 兼容及 ABI 2.0/2.1/2.2 合作式事件 |
| 宿主服务 | 六色逻辑帧显示、RAM 缓存、时间、定时器、受管 GATT 与单所有权 CoC |
| 切换与清理 | GPIO 4 配对确认/长按解绑，已绑定且无配对请求时短按仍选择已确认 ABI 2 APP |

当前 SDK 向 APP 提供网络连接状态、受管 GATT 和 CoC，尚未提供通用 Wi-Fi socket / HTTP client ABI。应用是受信任原生代码，不是隔离进程。

v2 不读取或接受旧 `push_token`，BLE op 1 永久拒绝；旧 `/api/push`、`/api/module`、`/api/wifi` 返回 410，不执行请求。`GET /health` 保留无显示副作用的存活探测。已配对 owner 继续通过 BLE 执行应用管理；Wi-Fi 配置仍可通过既有 SD/USB 维护，不能拿旧 token 绕过物理解绑。

相框 APP 保留自己的收图、校验、断线处理与完成回执；宿主提供通用通信、鉴权、OTA、调度和显示。2026-10-27 的 CoC 扩展改善连续传输，不把 PNG 业务移回宿主。当前验收范围见 [状态页](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Status-and-Evidence.md)。保留旧兼容通道，不继续扩展专用 PNG 宿主协议。

## 框架开发与验证

仅支持 N16R8、800×480 Spectra 6 PhotoPainter。固定 ESP-IDF `5e6f53cdb31fe5708eae3f55af9737be2822db22` 和未修改的 Registry elf_loader 1.3.3。

```sh
. /absolute/path/to/pinned-esp-idf/export.sh
git -C "$IDF_PATH" rev-parse HEAD
idf.py -B build-host -DSDKCONFIG=build-host/sdkconfig build
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/doctor.py
```

使用独立构建目录，避免旧 sdkconfig 缺少 BLE 配置；不要编辑 managed components 或生成产物。宿主不嵌入、也不要求构建兄弟业务 ELF。ABI 兼容的业务更新只需重新构建并安装 APP。

## 应用打包与管理

先构建目标 APP，然后在本仓库打包：

```sh
python3 tools/module.py package --app lifecycle --version 1 \
  --input ../photoframe/examples/lifecycle/build-app/lifecycle.app.elf \
  --output /tmp/lifecycle-v1.pkg
```

通过已完成物理配对的电脑端 BLE 客户端查询、暂存和激活：

```sh
ai-quota-frame ble status
ai-quota-frame ble install --file /tmp/lifecycle-v1.pkg
ai-quota-frame ble activate --app lifecycle
```

首次先执行客户端的 `ble pair` 并在相框上核对六位码、按功能键同意；后续命令使用持久关系，不填写设备令牌。Mac 使用已有应用身份。安装/激活是有状态操作，执行前核对设备、目录和授权范围。完整流程见 [应用包与恢复](docs-module-slots.md)，生命周期见 [运行层规范](docs-runtime-v2.md)。

## 文档入口与职责

- [宿主参考文档目录](docs/README.md)：协议、兼容接口与历史验收分开导航。
- [功能基线 SDK 原件](https://github.com/M1nt-Ch0c0/photopainter-host/blob/b4709c0/sdk/photopainter_app.h) 与 [该基线允许导入](https://github.com/M1nt-Ch0c0/photopainter-host/blob/b4709c0/sdk/abi2_imports.inc)：APP 二进制接口的依据。
- [常驻 BLE 协议](docs/ble-protocol.md)：发现、鉴权、包管理及应用特征桥接。
- [目录与 Wi-Fi](docs-multi-apps.md)：五 bank、网络配置与存储约束。
- [相框 APP](https://github.com/M1nt-Ch0c0/photoframe/tree/main)：输入格式、收图协议、应用示例。
- [电脑端](https://github.com/M1nt-Ch0c0/ai-quota-frame/tree/main)：采集、渲染、发送和部署命令。

历史 HTTP 合同保存在 [兼容输入参考](docs/legacy-image-input.md)，在 v2 已统一退役为 410。旧宿主 PNG opcode 仍可在新 BondAuth 会话后作为兼容路径使用；新图片业务继续走 photoframe APP。应用开发不能绕过 SDK、manager 或输入校验。

## 设备维护边界

应用 OTA 不写宿主 factory。当前项目不提供宿主整机 OTA，不改既有分区表。已具备五 bank 布局的设备，如另获授权更新框架，只写 `0x10000` 的宿主 bin；先准确识别设备、完整备份并校验 16 MiB Flash，有 microSD 时按项目要求备份。

打开原生 USB 串口可能导致复位，不用于日常在线检查。不要打印/提交真实配置、令牌日志、NVS 或 Flash/SD 备份。Wi-Fi 配置、恢复和历史迁移分别按对应指南处理，不能把旧迁移命令当成日常 APP 更新步骤。
