# 历史兼容输入：HTTP PNG 与宿主 BLE PNG

本页 HTTP 合同仅适用于历史宿主 `b4709c0` 等 v1 固件。**当前配对 v2 已把 HTTP 控制统一退役为 410，旧 token 不能继续控制设备。** 宿主 BLE PNG opcode 仍可在 BondAuth 后作兼容使用；新路线由 photoframe APP 通过受管 GATT/CoC 实现图片业务。应用操作命令见 [相框](https://github.com/M1nt-Ch0c0/photoframe/blob/main/README.md)及[电脑端](https://github.com/M1nt-Ch0c0/ai-quota-frame/blob/main/docs/ble.md)。

## 既有 HTTP 合同

`POST /api/push` 接受原始 `image/png`，最大 5 MiB，独立 Bearer；`X-PhotoPainter-App` 明确目标，省略时指向 photoframe。目标不是当前 APP 时不抢占选择，不驱动面板。

| 状态 | 含义 |
|---:|---|
| 200 | 此请求实体刷新和最终 POWER_OFF 等待成功 |
| 401 / 503 | 令牌缺失/错误 / 令牌未配置或运行资源不可用 |
| 409 | 操作忙碌或目标不是当前 APP |
| 413 / 415 / 422 | 超限 / 不支持的内容或输入能力 / 无效 PNG |
| 408 | PNG 请求体接收不完整或超时 |
| 504 | 显示 BUSY 等待超时；硬件可能已经开始工作，不盲目重刷 |

完整 PNG 解码和六色验证在应用中进行，全部通过后才访问面板。HTTP handler 借用的数据直到 manager 调用完成才释放；客户端断开不取消已经开始的显示。

## 既有宿主 BLE 扩展

opcode 15/16/17 是 capability、PNG begin 和结果查询，复用 CHUNK/COMMIT 等接收通道；最终调用相同 manager render。宿主每次启动保留四个 RAM 回执，断开重连可查询同一请求，改变启动代次或回执缺失时停止自动重画。

完整字节契约见 [BLE 协议的兼容扩展](ble-protocol.md#images-without-wi-fi-v1-additive-extension)。该实现保留，不代表未来 APP 收图继续沿用这些宿主业务 opcode。

## 回归约束

v1 记录中的 HTTP 401/409/200 不变成 v2 当前行为。v2 回归应验证旧入口带/不带 token 均为 410、无副作用；ABI 1/2 PNG 输入兼容、A/B 确认、启动保护、完整验色和物理成功语义仍保留。物理配对 v2 尚未实机验证，实际完成还需签名回执和人工目测。
