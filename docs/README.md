# 宿主框架参考文档

应用开发的统一入口是 [开发者 Wiki](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Home.md)。本目录保存宿主参考和可追溯的验证事实；相框业务输入与电脑端使用流程分别放在应用、客户端仓库。

当前配对 v2 的源码/离线构建已完成，**尚未实机验证**；旧认证固件的历史成功不代表新配对已通过。

## 当前参考

| 文档 | 内容 |
|---|---|
| [ABI 运行层](../docs-runtime-v2.md) | 生命周期、宿主服务、结果、调度与启动保护 |
| [包与 A/B](../docs-module-slots.md) | 应用构建、包、安装和回退，覆盖五 bank 和双 ABI |
| [目录与 Wi-Fi](../docs-multi-apps.md) | 存储布局和 SD/NVS 配置优先级 |
| [BLE 协议](ble-protocol.md) | 发现、HMAC、管理和应用特征桥接 |
| [物理配对 v2](ble-pairing-v2.md) | 六位码、功能键批准、持久关系、重置撤权与 BondAuth |
| [ABI 2.2 CoC](abi-2-2-coc.md) | 单前台所有权、非阻塞记录、会话、背压和无线封套 |
| [历史输入](legacy-image-input.md) | v1 HTTP 合同；v2 HTTP 已退役，宿主 BLE PNG 仅兼容 |

## 验证记录与历史

这些文件记录发生时的提交、设备条件、已测和未测范围，不是逐步部署教程：

- [2026-09-06 显示验证](validation-2026-09-06.md)
- [多应用本地验证](validation-multi-app-local.md)与[实机验证](validation-multi-app-hardware.md)
- [ABI 2 运行层验证](validation-runtime-v2.md)
- [Windows BLE 验证](validation-ble.md)
- [Mac 离线图片、回执及内存](validation-ble-images.md)
- [2026-10-29 配对 v2 离线验证](validation-pairing-v2-2026-10-29.md)：尚未实机验证

photoframe v20 已在 2026-10-27 使用 ABI 2.2 CoC 实机收图并返回物理完成回执。源码发布、最新设备目录及剩余验证范围见 [状态页](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Status-and-Evidence.md)。旧测试通过不能计为新代码全面回归。
