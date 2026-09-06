# 2026-09-06 联调验证记录

硬件为 ESP32-S3 PhotoPainter N16R8、7.3 英寸 800×480 Spectra 6；ESP-IDF 固定为
`5e6f53cdb31fe5708eae3f55af9737be2822db22`，Registry elf_loader 1.3.3 未修改。
以下是一次真实设备部署结果，不代替后续版本的回归验证。

| 项目 | 结果 |
|---|---|
| 框架解耦 | 宿主独立构建，无嵌入业务 ELF 或 sibling 构建依赖；固定 ABI 1 导出 |
| 包校验 | 真机错误 SHA 返回 422；缺失导入候选激活返回 409 并恢复原槽 |
| 未确认重启 | 候选试运行时复位，重新启动已确认槽并清除 trial/pending |
| 两槽确认 | v14、v15 分别成功刷屏后确认，POST /api/push 返回 200 |
| 手动回退 | v15 → v14，重启后仍为 v14；随后切回 v15 |
| 最终状态 | active=A/v15，previous=B/v14，pending=-1，trial=0，ready=true |
| 刷新时序 | 刷新 BUSY 约 19.16 秒，最终 POWER_OFF BUSY 约 150 毫秒 |
| 视觉确认 | 用户确认 Codex/Grok/Kimi 页面、方向、文字及颜色正常 |
| 数据来源 | 远端 CLIProxyAPI + CPAMP 经本机 SSH loopback 隧道；非演示数据 |
| 本地服务 | launchd 启动、受控重启后恢复、两个连续 5 分钟周期均真实推图成功 |

服务成功事件为 11:48:37 和 11:53:38（Asia/Shanghai）。`frame pushed` 仅在收到
设备推图 HTTP 200 后写入日志，不能用模块管理接口的 200 替代。

## 修复与边界

旧驱动配置 ALDO3，但官方原理图将 ALDO3 接至 `Audio_VCC`，ALDO4 接至
`EPD_VCC`。修正为电压寄存器 `0x95`、使能寄存器 `0x90` bit 3 后，刷屏成功。
配置使用读改写并保留其他电源位。误切 ALDO3 的诊断曾导致 PMIC I²C 不再应答，
用户实体关机重启后恢复；临时恢复写入和未验证的时序实验没有进入最终驱动。

macOS 常驻进程首次连接曾出现 `no route to host`，随后重试成功；没有证据仅凭
这个错误归因为权限。安装器补充原生应用身份、本地网络用途说明及
`AssociatedBundleIdentifiers`。机器需保持开机、登录和联网；不保证断电期间更新。

默认软件测试使用生成的结构化 ELF 夹具，无需预先构建相邻仓库；设置
`PHOTOFRAME_TEST_ELF` 可额外验证真实 app ELF。格式与状态测试覆盖边界、损坏、
Flash 写入和 NVS 提交失败；组件原生测试、app ELF / so 构建及服务 `make check`
另行通过。测试命令见各仓库 AGENTS.md 和 [双槽说明](../docs-module-slots.md)。

测试从以下基线加本次变更进行：host `4f1029f4a88db75757776fc31df19e2e69a9d83a`、
component `43f4019ac9694a9b4c9e9b6821a3a4cc83538240`、
service `26137c3e39463e0dfaebc29c18f9904f9524bb36`。
部署时宿主 ELF SHA-256 为
`41a1a9f51e5cf06b87465c787f2b3dd883d052dfe7d9d5ec5a13507b3d3c90dc`，与设备启动报告一致。
业务版本号是包标签，不是 Git 提交号。

完整 16 MiB Flash 备份已进行 SHA-256 与设备 verify-flash 校验；本次 SD 备份已获
用户豁免。含 Wi-Fi、令牌、管理密钥的配置、NVS/Flash 备份和原始日志只保存在本机，
不随此记录入库。
