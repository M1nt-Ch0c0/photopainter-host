# PhotoPainter Host 应用框架

本仓库是 ESP32-S3 PhotoPainter 的常驻框架：负责设备通信、鉴权、独立应用安装、ELF 加载、统一调度、切换与宿主服务。业务页面、图片解码和应用自身协议属于 APP。

**开发从 [PhotoPainter 开发者 Wiki](https://github.com/M1nt-Ch0c0/esp32s3/blob/codex/framework-wiki/wiki/Home.md) 开始。** AI 协作先读 [AGENTS.md](AGENTS.md) 和项目 [Skill](.agents/skills/develop-photopainter-stack/SKILL.md)。

## 框架能力

| 部分 | 已提供的能力 |
|---|---|
| 通信与鉴权 | Wi-Fi 配置/连接、HTTP 管理、常驻 NimBLE 外设、设备发现和双向 HMAC |
| 应用管理 | 五个 APP、各自 A/B、备用槽上传、显式试运行、确认、回滚和有界启动恢复 |
| 运行层 | 单 ELF 前台运行，ABI 1 兼容及 ABI 2.0/2.1 合作式事件 |
| 宿主服务 | 六色逻辑帧显示、RAM 缓存、时间、定时器和受管应用 BLE |
| 切换与清理 | GPIO 4 选择已确认 ABI 2 APP，STOP 后撤销定时/BLE/事件再卸载 |

当前 SDK 向 APP 提供网络连接状态和受管 BLE，尚未提供通用 Wi-Fi socket / HTTP client ABI。应用是受信任原生代码，不是隔离进程。

2026-10-23 确定的开发方向是：相框 APP 通过 ABI 2.1 实现分块收图、校验、断线处理与完成回执，宿主继续提供通用通信、鉴权、OTA、调度和显示。该 APP 功能仍在独立开发，见 [设计与验收要求](https://github.com/M1nt-Ch0c0/esp32s3/blob/codex/framework-wiki/wiki/BLE-Image-App-Roadmap.md)。保留旧兼容通道，不继续扩展专用 PNG 宿主协议。

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

通过电脑端 BLE 客户端或现有 HTTP 管理接口查询、暂存和激活，二者使用同一状态机：

```sh
ai-quota-frame ble status
ai-quota-frame ble install --file /tmp/lifecycle-v1.pkg
ai-quota-frame ble activate --app lifecycle
```

命令需通过可信启动器加载独立设备令牌；Mac 使用已有应用身份。安装/激活是有状态操作，执行前核对设备、目录和授权范围。完整流程见 [应用包与恢复](docs-module-slots.md)，生命周期见 [运行层规范](docs-runtime-v2.md)。

## 文档入口与职责

- [宿主参考文档目录](docs/README.md)：协议、兼容接口与历史验收分开导航。
- [SDK 原件](sdk/photopainter_app.h) 与 [允许导入](sdk/abi2_imports.inc)：APP 二进制接口的依据。
- [常驻 BLE 协议](docs/ble-protocol.md)：发现、鉴权、包管理及应用特征桥接。
- [目录与 Wi-Fi](docs-multi-apps.md)：五 bank、网络配置与存储约束。
- [相框 APP](https://github.com/M1nt-Ch0c0/photoframe/tree/codex/framework-wiki)：输入格式、收图协议、应用示例。
- [电脑端](https://github.com/M1nt-Ch0c0/ai-quota-frame/tree/codex/framework-wiki)：采集、渲染、发送和部署命令。

历史 `/api/push` 与宿主 PNG opcode 保持兼容，合同与错误码见 [兼容输入参考](docs/legacy-image-input.md)；它们不再作为框架入门的业务操作教程。应用开发不要绕过 SDK、manager 或旧版兼容校验。

## 设备维护边界

应用 OTA 不写宿主 factory。当前项目不提供宿主整机 OTA，不改既有分区表。已具备五 bank 布局的设备，如另获授权更新框架，只写 `0x10000` 的宿主 bin；先准确识别设备、完整备份并校验 16 MiB Flash，有 microSD 时按项目要求备份。

打开原生 USB 串口可能导致复位，不用于日常在线检查。不要打印/提交真实配置、令牌日志、NVS 或 Flash/SD 备份。Wi-Fi 配置、恢复和历史迁移分别按对应指南处理，不能把旧迁移命令当成日常 APP 更新步骤。
