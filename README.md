# PhotoPainter Host

> **AI / 开发者入口：**先阅读 [`AGENTS.md`](AGENTS.md)。空白电脑部署、三仓联调和真机诊断使用项目级 Skill：[`develop-photopainter-stack`](.agents/skills/develop-photopainter-stack/SKILL.md)。

## 刷写

本固件只适用于 7.3 英寸、800×480、N16R8 的 ESP32-S3 PhotoPainter。刷写前先把 16 MiB Flash 完整读出并校验 SHA-256；若机内有 microSD，也应关机取卡、用读卡器制作整卡镜像并校验。备份文件放在仓库之外。

将 `photopainter-host` 与 `photoframe` 检出到同一父目录，使用 ESP-IDF commit `5e6f53cdb31fe5708eae3f55af9737be2822db22`：

```bash
. /path/to/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/serial/by-id/your-device flash monitor
```

组件由构建系统从相邻的 `../photoframe` 编译成 `photoframe.app.elf` 并嵌入宿主；`elf_loader` 固定通过 Espressif Component Registry 的 `^1.3.3` 解析，不需要 fork。可用 `PHOTOFRAME_COMPONENT_DIR` 指向另一个本地检出。

首次刷写后创建本地密钥文件并写入 NVS；`secrets.env` 已被忽略：

```bash
cp secrets.env.example secrets.env
chmod 600 secrets.env
# 编辑 WIFI_SSID、WIFI_PASSWORD、PUSH_TOKEN
./tools/provision.py --port /dev/serial/by-id/your-device --config secrets.env
```

固件不读写 microSD，也不提供 OTA。

## 推图

设备连入 2.4 GHz Wi-Fi 后，只接受不超过 5 MiB 的 800×480 六色原始 PNG：

```bash
curl --fail-with-body \
  -H "Authorization: Bearer ${PUSH_TOKEN}" \
  -H 'Content-Type: image/png' \
  --data-binary @frame.png \
  http://DEVICE_IP/api/push
```

响应 `200` 表示电子纸已经完成刷新并关断面板电源；典型需要约 25 秒。无效 PNG、错误尺寸或六色以外的像素在驱动屏幕前即被拒绝。设备 USB/Wi-Fi 保持常亮，不进入深睡眠。

## 鉴权

`PUSH_TOKEN` 是 32–128 个无空白可打印 ASCII 字节的独立 Bearer 码，不得复用 Wi-Fi 密码或主机侧任何 OAuth/管理密钥。设备尚未配置推图码时返回 `503`，缺失或错误码返回 `401`，请求体超过 5 MiB 返回 `413`。失败请求不会改变屏幕。

不要提交 `secrets.env`、生成的 NVS 镜像或含密钥的命令输出，也不要把设备 HTTP 端口映射到公网。
