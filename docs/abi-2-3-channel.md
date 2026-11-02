# ABI 2.3 通用消息通道与首个 GATT 适配器

状态：已构建、装机并完成手机 Web GATT 推图验收；67,626 B 上传 7.9 秒，刷新等待 20.3 秒，签名完成回执及人眼方向/颜色确认均通过。此结论不包含手机原生 CoC 或 Wi-Fi。

应用注册逻辑服务 ID，通过 CHANNEL 事件和 info/recv/send/close 交换完整消息。
应用看不到 ATT MTU、GATT characteristic、CoC PSM 或 socket。宿主复制数据，
唯一 Manager 调用应用，不保留 ELF 回调或跨事件的应用指针。旧 ABI 2.0–2.2 的
结构和符号保持原状；导入 channel 符号的 ELF 必须声明 minor >= 3。

首版采用一个授权对端、一个请求/回复窗口。注册最大消息 4096 B、最大回复 256 B；
两端缓冲在注册时分配，队列满返回 AGAIN，读缓冲不足返回 TOO_SMALL 并保留记录。
配置/关闭/卸载改变 revision；每次新连接得到不重用的 session。适配器必须在
认证成功后授权 peer epoch；撤销或断开立即禁止排队 OPEN/DATA 使用旧 epoch。
已经开始的同步显示继续持有借用和 Manager 准入，不能因断连释放其输入。

## GATT 线协议

保留物理配对 v2、连接随机数、逐请求序号与现有双向 HMAC。
此阶段提供认证和完整性，**没有消息加密，不承载 Wi-Fi 密码**。
提案中的 AEAD、公共 Go/WASM 会话核心、TCP/WebSocket 和跨通道恢复仍为后续工作。

固定宿主服务新增只读 characteristic `7b9d0007-6c82-4f31-9a48-6f5061696e74`：
6 B，`version:u8=1, request_max:u16le=4096, response_max:u16le=512, flags:u8=1`。
flags bit0 表示 RX 支持 Write Without Response。原 0006 的 3 B MTU 容量契约不变。
RX 0004 同时支持 Write Request 和 Write Command，TX 0005 仍为可读响应。

所有命令先认证。下表的 payload 被完整包含在既有请求 HMAC 中，整数为小端：

| Opcode | 请求 payload | 成功响应 payload |
|---|---|---|
| 24 CHANNEL_INFO | 空 | generation:u32, revision:u32, message_max:u32, reply_max:u32, service_id:16 B |
| 25 CHANNEL_OPEN | generation:u32, revision:u32 | session:u32 |
| 26 CHANNEL_EXCHANGE | generation:u32, revision:u32, session:u32, message:1..4047 B | 应用回复，至多 256 B |
| 27 CHANNEL_CLOSE | generation:u32, revision:u32, session:u32 | 空 |

只有 opcode 26 可使用 4096 B 逻辑请求；其余命令保持原有 512 B 限制。
响应保持 512 B 上限。单个 ATT value 至多 512 B，仍用 offset:u16/total:u16 的
4 B 分片头；逻辑请求 4096 B 包含 op/sequence/HMAC 与 12 B 通道定位字段。
应用可用消息上限为 `min(注册上限, 4096-37-12)`，不是 ATT MTU。

网页按协商容量连续提交有界消息的 Write Command 分片，最后一个分片使用
Write Request，随后读取并校验完整响应。应用先消费整个消息才给累计偏移回执。
每个数据消息一次往返，不再 APP_WRITE 后追加独立 APP_READ。未协商新能力的旧
应用继续使用已发布接口；新应用遇到能力不符会明确失败，不在 I/O 错误后静默降级。

## 相框应用

PFI1 的 BEGIN/DATA/COMMIT/QUERY/CANCEL 和 64 B 回执格式不变。
`image_transfer.c` 是不依赖 SDK/无线栈的事务核心，原 GATT、CoC 和新 CHANNEL
适配器共用它。适配器执行单写入者准入，不能混合不同通道/会话的活动输入。
GATT 通用通道每消息最多含 4025 B 图片；旧 GATT 仍为 106 B。
COMMIT 完整校验 SHA 和 PNG 后才显示；返回 COMPLETE 必须已经完成刷新和 POWER_OFF。

客户端在发送 COMMIT 前持久保存原事务身份。丢失 COMMIT 回复只 QUERY 原事务；
断线不会换 ID 重发或重复显示。当前仍在 RAM 保留 4 个完成回执；重启丢失接收和
回执，结果未知时需要人工核对。尚未实现跨传输恢复接收。

## 离线验收

宿主原生测试覆盖：消息/回复边界、复制与非借用、窗口背压、TOO_SMALL 不消费、
代次/修订/会话作废、撤销 peer 后拒绝排队操作；生产 Manager 处理 3 KiB 消息，
稳态 DATA 及成功 TIMER 不读取 ELF 槽。协议测试使用 ASan/UBSan，覆盖完整
4096 B 分片重组、末字节篡改、旧 opcode 的超长请求拒绝和签名回复。

相框测试通过三种适配器运行真实解码与完成回执，并验证混用拒绝和 COMMIT 去重。
网页测试覆盖能力协商、4096 B 分片、最后分片接收确认、应用消息预算、持久化
COMMIT、丢失回复后的 QUERY。具体安装摘要和手机实测另行记录，不能用这些
测试替代实机性能或供电稳定性证据。
