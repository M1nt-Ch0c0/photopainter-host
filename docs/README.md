# 宿主框架参考文档

应用开发的统一入口是 [开发者 Wiki](https://github.com/M1nt-Ch0c0/esp32s3/blob/codex/framework-wiki/wiki/Home.md)。本目录保存宿主参考和可追溯的验证事实；相框业务输入与电脑端使用流程分别放在应用、客户端仓库。

## 当前参考

| 文档 | 内容 |
|---|---|
| [ABI 运行层](../docs-runtime-v2.md) | 生命周期、宿主服务、结果、调度与启动保护 |
| [包与 A/B](../docs-module-slots.md) | 应用构建、包、安装和回退，覆盖五 bank 和双 ABI |
| [目录与 Wi-Fi](../docs-multi-apps.md) | 存储布局和 SD/NVS 配置优先级 |
| [BLE 协议](ble-protocol.md) | 发现、HMAC、管理和应用特征桥接 |
| [兼容输入](legacy-image-input.md) | 旧 HTTP/宿主 PNG 的合同与安全约束，不是新 APP 业务开发路径 |

## 验证记录与历史

这些文件记录发生时的提交、设备条件、已测和未测范围，不是逐步部署教程：

- [2026-09-06 显示验证](validation-2026-09-06.md)
- [多应用本地验证](validation-multi-app-local.md)与[实机验证](validation-multi-app-hardware.md)
- [ABI 2 运行层验证](validation-runtime-v2.md)
- [Windows BLE 验证](validation-ble.md)
- [Mac 离线图片、回执及内存](validation-ble-images.md)

APP 自身收图路线尚待功能分支实现，见 [路线与验收](https://github.com/M1nt-Ch0c0/esp32s3/blob/codex/framework-wiki/wiki/BLE-Image-App-Roadmap.md)。旧测试通过不能计为新路线验收。
