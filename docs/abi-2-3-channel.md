# ABI 2.3 消息通道规格

这一页是通用消息通道（ABI 2.3）在宿主一侧的规格：客户端访问它的四个管理命令（op 24–27）的字节格式、大小限制、会话和失效规则，以及宿主把消息交给应用的方式。应用一侧的 C 接口（`app_host_channel_*_v23`、配置和事件结构、返回码）见 [应用接口参考](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/05-app-api-reference.md#消息通道-channelabi-23)；它与 GATT、CoC 两种方式的对比见 [蓝牙通信](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/06-bluetooth.md#8-应用通信的三种方式)。

## 设计

消息通道是一个与传输方式无关的消息邮箱。应用注册一个 16 字节的逻辑服务 ID，之后按“一条请求、一条回复”的方式和一个已鉴权的客户端交换完整消息。应用看不到 ATT MTU、GATT 特征、CoC PSM 之类的细节。

- 当前宿主通过管理服务的签名命令（op 24–27）承载它，所以它和其他管理命令共用鉴权、序号和签名。
- 同一时间只有一个对端、一条在途消息：上一条消息还没被应用取走、或上一条回复还没送出时，新消息会被拒绝。
- 应用注册时宿主分配两块缓冲：消息最多 4096 字节（`APP_CHANNEL_MAX_MESSAGE`），回复最多 256 字节（`APP_CHANNEL_MAX_REPLY`）。宿主复制所有数据，不保留指向应用的指针或回调。
- 网页插件用它推图，是当前唯一使用它的客户端。

实现位置：`main/app_channel.c`（注册、缓冲、会话）、`main/app_manager.c` 的 `CHANNEL_REQUEST`（op 24–27 的处理）、`main/ble_transport.c`（对端授权）。

## 大小限制

op 26 是唯一允许使用 4096 字节逻辑帧的命令（`PP_BLE_FRAME_MAX`），其他命令仍限 512 字节。4096 字节里要放：

| 部分 | 字节 |
|---|---:|
| `op` | 1 |
| `sequence` | 4 |
| `generation`、`revision`、`session` | 12 |
| 消息 | 最多 4047 |
| HMAC | 32 |

所以客户端一次能发的消息上限是 `min(应用注册的 max_message_bytes, 4047)`，op 24 直接给出这个值。回复仍受 512 字节的回复帧限制，应用回复本身最多 256 字节。

客户端发送大帧时按 `7b9d0006` 给出的写入容量分片（见 [分片写入](ble-protocol.md#分片写入)）。`7b9d0007` 的 `flags` 第 0 位表示可以用无应答写：网页插件把前面的分片用无应答写发出，最后一片用带应答的写，然后读回复。

## 命令

以下命令都需要 BondAuth 后的签名会话，表中只列 `payload`。

| op | 名称 | 请求 | 成功回复 |
|---:|---|---|---|
| 24 | CHANNEL_INFO | 空 | `generation:u32, revision:u32, message_max:u32, reply_max:u32, service_id:16` |
| 25 | CHANNEL_OPEN | `generation:u32, revision:u32` | `session:u32` |
| 26 | CHANNEL_EXCHANGE | `generation:u32, revision:u32, session:u32, message:1..4047` | 应用回复，1–256 字节 |
| 27 | CHANNEL_CLOSE | `generation:u32, revision:u32, session:u32` | 空 |

- **generation** 是应用的加载代次，**revision** 是通道的注册修订号。应用重新加载、重新注册或关闭通道后，旧的组合失效，命令返回 8。
- **session** 由 op 25 分配，每次新会话都不同，不会重复使用。同一个对端再发 op 25 会拿到当前会话的编号。
- 四个命令都要取得准入锁，拿不到返回 3。前台应用没有就绪或没有注册通道时返回 8。

### 客户端流程

1. 读 `7b9d0007`，确认支持 4096 字节逻辑帧。
2. 读状态（op 3），确认 `payload_abi_minor >= 3`。
3. op 24：核对 `service_id` 是期望的应用服务，记下 `generation`、`revision` 和 `message_max`。
4. op 25：取得 `session`。
5. 反复 op 26：每条消息一次往返，回复就是应用的应答。
6. op 27：关闭会话。

### op 26 的执行过程

宿主收到 op 26 后，在 App Manager 里同步完成：

1. 核对 generation、revision 和 session；
2. 把消息复制进通道的接收缓冲；
3. 立即给应用派发带 `APP_CHANNEL_RX_READY` 的 CHANNEL 事件；
4. 事件返回后，取出应用在这个事件里用 `send` 写下的回复，作为 op 26 的回复负载。

所以应用必须在同一个 CHANNEL 事件里给出回复。

| 情况 | 结果 |
|---|---|
| 上一条消息还没取走或上一条回复还没送出 | 3 |
| 消息为空，或超过 `message_max` | 4 |
| generation、revision、session 不匹配，或会话已关闭 | 8 |
| 应用处理事件出错 | 10 |
| 应用在事件里没有给出回复 | 8 |

## 对端与授权

- 宿主把“已通过 BondAuth 的连接”登记为通道的授权对端。同一时间只有一个对端能持有会话。
- 新的已鉴权连接出现时，旧对端的会话关闭，应用收到 `APP_CHANNEL_DISCONNECTED`。
- 连接断开、长按重置配对，都会立即撤销授权，排队中使用旧授权的 OPEN 和消息不会执行。
- 已经开始的显示继续占着准入锁并完成，断开连接不会提前释放它借用的数据。

## 应用侧要点

完整接口见 [应用接口参考](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/05-app-api-reference.md#消息通道-channelabi-23)，这里只列与线上行为直接相关的几条：

- 用 `APP_MANIFEST_CHANNEL` 声明 ABI 2.3，导入 `app_host_channel_*` 的应用必须声明次版本 ≥ 3。
- `max_message_bytes` 取 1–4096，`max_reply_bytes` 取 1–256，`service` 不能全为 0。
- `recv` 一次取出整条消息；缓冲不够时返回 `APP_CHANNEL_TOO_SMALL` 并保留消息。
- 注册跟随本次加载，`close`、切换和卸载都会释放缓冲并改变 revision。

## 安全边界

消息通道沿用管理服务的鉴权和完整性保护：每条消息都包在签名命令里，带序号，不能被篡改或重放。消息内容不加密，不要用它传 Wi-Fi 密码等秘密。
