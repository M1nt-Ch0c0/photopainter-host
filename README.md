# PhotoPainter Host

> **AI / 开发者入口：**先阅读 [`AGENTS.md`](AGENTS.md)。空白电脑部署、三仓联调和真机诊断使用项目级 Skill：[`develop-photopainter-stack`](.agents/skills/develop-photopainter-stack/SKILL.md)。

四个仓库的职责、架构图、耦合边界与修改影响，见 [PhotoPainter 架构总览](https://github.com/M1nt-Ch0c0/esp32s3/blob/codex/multi-wifi-apps/ARCHITECTURE.md)。

当前多 Wi-Fi（SD JSON / NVS）与多应用独立 A/B 安装、切换、更新命令，见 [多应用与多 Wi-Fi 指南](docs-multi-apps.md)。

## 刷写

本固件只适用于 7.3 英寸、800×480、N16R8 的 ESP32-S3 PhotoPainter。刷写前先把 16 MiB Flash 完整读出并校验 SHA-256；若机内有 microSD，也应关机取卡、用读卡器制作整卡镜像并校验。备份文件放在仓库之外。

使用 ESP-IDF commit `5e6f53cdb31fe5708eae3f55af9737be2822db22` 构建独立框架：

```bash
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

业务 ELF 独立保存在 A/B 数据槽，通过鉴权接口更新和试运行；首次成功刷屏后确认，确认前重启或运行失败回退。框架不再编译或嵌入 `photoframe`。首次迁移涉及分区表、槽 A 种子和独立状态分区，请先阅读 [双槽部署与回退说明](docs-module-slots.md)。后续业务更新不需要重刷主程序。

首次刷写后创建本地密钥文件并写入 NVS；`secrets.env` 已被忽略：

```bash
cp secrets.env.example secrets.env
chmod 600 secrets.env
# 编辑 WIFI_SSID、WIFI_PASSWORD、PUSH_TOKEN
./tools/provision.py --port /dev/serial/by-id/your-device --config secrets.env
```

固件启动时可读取或首次迁移 SD Wi-Fi 配置；应用包保存在内部 Flash，不依赖 SD，不提供整机 OTA。

## 推图

设备连入 2.4 GHz Wi-Fi 后，只接受不超过 5 MiB 的 800×480 六色原始 PNG：

```bash
python3 tools/module.py push --url http://DEVICE_IP --input frame.png
```

响应 `200` 表示电子纸已经完成刷新并关断面板电源；典型需要约 25 秒。无效 PNG、错误尺寸或六色以外的像素在驱动屏幕前即被拒绝。设备 USB/Wi-Fi 保持常亮，不进入深睡眠。

## 鉴权

工具私下读取忽略的 `secrets.env`，无需把令牌放进命令行。`PUSH_TOKEN` 是 32–128 个无空白可打印 ASCII 字节的独立 Bearer 码，不得复用 Wi-Fi 密码或主机侧任何 OAuth/管理密钥。设备尚未配置推图码时返回 `503`，缺失或错误码返回 `401`，请求体超过 5 MiB 返回 `413`。输入拒绝不会改变屏幕；驱动执行中途的硬件故障可能已影响面板，不应盲目重试。

完整的首次部署、模块更新和手动回退命令见 [双槽说明](docs-module-slots.md)；实机验证结果与已修复的电源问题见 [验证记录](docs/validation-2026-09-06.md)。

不要提交 `secrets.env`、生成的 NVS 镜像或含密钥的命令输出，也不要把设备 HTTP 端口映射到公网。

多应用真实安装、刷屏、回退和多 Wi-Fi 顺序尝试的最新证据见 [多应用实机记录](docs/validation-multi-app-hardware.md)。
