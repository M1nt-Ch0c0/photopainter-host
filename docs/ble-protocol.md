# 宿主蓝牙线上协议

这一页是宿主固件蓝牙管理协议的字节级规格：广播内容、管理服务的特征、分片写入、签名请求的格式、错误码、全部管理命令和状态 JSON。写客户端、改 `main/ble_protocol.c` 或排查线上数据时查这一页。

协议的整体设计（为什么这样鉴权、图片事务怎样避免重复刷屏、三种应用通信方式怎么选）见 [蓝牙通信](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/06-bluetooth.md)。配对和重连鉴权（op 18–23）在 [配对 v2](ble-pairing-v2.md)，通用消息通道（op 24–27）在 [ABI 2.3 消息通道](abi-2-3-channel.md)，CoC 通道在 [ABI 2.2 CoC](abi-2-2-coc.md)。

## 约定

- 所有多字节整数都是小端序。
- HMAC 指 HMAC-SHA256，标签是完整的 32 字节，宿主用常数时间比较。
- 域名字符串（例如 `pp-ble-v1/request`）按 ASCII 直接拼在数据前面，不带结尾的 NUL。
- 应用 ID 在线上统一编码为 32 字节：1–31 个小写字母、数字、`_` 或 `-`，后面用 NUL 补齐。

实现位置：

| 文件 | 内容 |
|---|---|
| `main/ble_protocol.c`、`main/ble_protocol.h` | 分片重组、签名请求、op 2–17 和 op 24–28 的分发。纯 C，不依赖 RTOS 和无线栈 |
| `main/ble_transport.c` | NimBLE 适配：广播、GATT 特征、传输工作线程、应用 GATT 服务的发布 |
| `main/pairing.c`、`main/app_pairing.c` | 配对 v2 和 BondAuth（op 18–23） |
| `main/ble_shell.c`、`main/host_shell.c` | 只读诊断命令（op 28） |
| `main/app_manager.c` | 状态 JSON、管理命令和应用请求的串行执行 |

## 广播与发现记录

相框只做外设，同一时间只接受一个连接，广播间隔 200–300 毫秒。

- **广播数据**：标志 `0x06`；完整的 128 位服务 UUID `7b9d0001-6c82-4f31-9a48-6f5061696e74`；完整设备名 `PP-XXXX`。`XXXX` 是设备 ID 最后两个字节的大写十六进制，例如设备 ID `001122334455` 的设备名是 `PP-4455`。
- **扫描响应**：厂商数据，公司 ID `0xffff`（开发用标识，不是蓝牙联盟分配的），后面跟 20 字节发现记录。

发现记录：

| 偏移 | 字节 | 内容 |
|---:|---:|---|
| 0 | 1 | 协议版本，固定为 `2` |
| 1 | 1 | Wi-Fi 状态：0 离线、1 连接中、2 已获得 IPv4、3 Wi-Fi 配置无效 |
| 2 | 6 | 设备 ID：出厂 Wi-Fi 站点 MAC |
| 8 | 4 | IPv4 地址（网络字节序），只有状态为 2 时非零 |
| 12 | 8 | SSID 的 SHA-256 前 8 字节，状态为 1 或 2 时非零 |

Wi-Fi 状态或 IP 变化时，记录立即更新。连接期间广播暂停，断开后用最新记录重新广播。记录没有签名，客户端只用它选设备；BondAuth 成功后，签名回复里会再带一份记录供核对。

## 管理服务的特征

服务 UUID 是 `7b9d0001-6c82-4f31-9a48-6f5061696e74`，下表特征 UUID 都以 `-6c82-4f31-9a48-6f5061696e74` 结尾。

| UUID 前缀 | 名称 | 属性 | 内容 |
|---|---|---|---|
| `7b9d0002` | Discovery | 读 | 20 字节发现记录，无需鉴权 |
| `7b9d0003` | Challenge | 读 | 本次连接的 32 字节随机数，每次连接重新生成 |
| `7b9d0004` | Request | 写、无应答写 | 分片写入请求帧 |
| `7b9d0005` | Response | 读（支持长读） | 最近一次请求的完整回复，最多 512 字节 |
| `7b9d0006` | Write capacity | 读 | 3 字节：`version:u8=1, write_capacity:u16` |
| `7b9d0007` | Message limits | 读 | 6 字节：`version:u8=1, request_max:u16=4096, response_max:u16=512, flags:u8=1` |

- `write_capacity` 是本连接一次 ATT 写入能带的字节数（含 4 字节分片头），等于 `min(512, ATT_MTU - 3)`。MTU 247 时为 244，最小 MTU 23 时为 20。客户端每次连接都应先读它。
- `7b9d0007` 的 `flags` 第 0 位为 1，表示 Request 特征支持无应答写（Write Command）。

## 分片写入

一个请求帧（逻辑帧）通过一次或多次写 `7b9d0004` 送达。每次写入的格式：

```
offset:u16 | total:u16 | 帧数据（至少 1 字节）
```

| 规则 | 说明 |
|---|---|
| 单次写入长度 | 5 到 `write_capacity` 字节 |
| 帧长度 `total` | 普通命令最多 512 字节；op 26 最多 4096 字节（`PP_BLE_FRAME_MAX`） |
| 顺序 | `offset` 必须等于已收到的长度。`offset=0` 且 `total` 与当前帧不同时，重新开始一帧 |
| 重复分片 | 与已收内容完全相同的重复分片会被接受 |
| 错误 | 空洞、内容冲突的重叠、越界或长度不对时，这次写入得到 ATT 错误，已收到的部分作废 |
| 处理中 | 上一帧还在处理时写入，得到 ATT 错误 |

收齐最后一字节后，宿主把整帧交给传输工作线程处理。

## 读取回复

写完最后一个分片后，客户端反复读 `7b9d0005`：

- 读到**单个 `0x00` 字节**：请求还在处理，稍后再读（命令行客户端间隔 25 毫秒）。这个字节只表示“还没好”。
- 读到更长的内容：这就是完整回复。它在下一个请求完成前保持不变。

同一时间只能有一个请求在途，读到回复后才能发下一个。

## 签名请求与回复

BondAuth（op 23，见 [配对 v2](ble-pairing-v2.md)）成功后得到 32 字节会话密钥 `K`。之后每条命令都用 `K` 签名：

```
请求：op:u8 | sequence:u32 | payload | HMAC(K, "pp-ble-v1/request"  || 前面所有字节)
回复：op:u8 | sequence:u32 | code:u16 | payload | HMAC(K, "pp-ble-v1/response" || 前面所有字节)
```

| 规则 | 说明 |
|---|---|
| 序号 | 从 1 开始，每条加 1，不能回绕。回复带回同一序号 |
| 请求上限 | 普通命令帧最多 512 字节，负载最多 475 字节；op 26 帧最多 4096 字节 |
| 回复上限 | 512 字节 |
| 未签名回复 | 7 字节（`op, sequence, code`），出现在格式错误、未鉴权、签名或序号不对、会话过期等情况。未签名回复不能当作成功或身份证明 |
| 签名校验失败 | 客户端应结束会话并断开。新连接上不能重发旧的签名命令；操作结果不确定时先读状态 |

会话有效期：

- 60 秒内没有有效命令，会话过期；
- 从鉴权开始最多 2 小时；
- 宿主正在执行的长操作（例如刷屏）期间不计空闲时间，操作结束后重新计时；
- 会话过期时宿主主动断开连接；
- 断开连接会丢弃鉴权状态和已经收到一半的安装包或图片。重连后要从头重传，宿主不做跨连接续传。

## 错误码

回复里的 `code`：

| 代码 | 常量 | 含义 |
|---:|---|---|
| 0 | `PP_BLE_OK` | 成功 |
| 1 | `PP_BLE_UNAUTHORIZED` | 签名、序号或鉴权尝试无效 |
| 2 | `PP_BLE_UNCONFIGURED` | 没有配对关系 |
| 3 | `PP_BLE_BUSY` | 准入锁被占用（按键、定时器或网络事件、另一个上传），稍后重试 |
| 4 | `PP_BLE_INVALID` | 长度、ID 或 op 不合法，或 op 不支持 |
| 5 | `PP_BLE_OFFSET` | 偏移不对：空洞、内容冲突的重复数据，或分页偏移越界 |
| 6 | `PP_BLE_HASH` | 整包 SHA-256 不匹配 |
| 7 | `PP_BLE_MEMORY` | 内存不足 |
| 8 | `PP_BLE_STATE` | 目录或传输状态不允许这个操作，或应用不存在 |
| 9 | `PP_BLE_PACKAGE` | 安装器拒绝了这个包 |
| 10 | `PP_BLE_STORAGE` | 存储或应用操作失败 |
| 11 | `PP_BLE_EXPIRED` | 会话过期 |
| 12 | `PP_BLE_IMAGE_INVALID` | PNG 无效，在接触屏幕前被拒绝 |
| 13 | `PP_BLE_DISPLAY_FAILED` | 物理显示失败 |
| 14 | `PP_BLE_BOOT_CHANGED` | 图片请求属于相框的上一次开机 |

## 命令总表

| op | 命令 | 说明 |
|---:|---|---|
| 1 | 旧固定令牌鉴权 | 总是返回未签名的错误 1 |
| 2 | Discovery（签名版） | 见下文 |
| 3 | 状态 | 分页读取状态 JSON |
| 4–8 | 上传应用包 | 开始、数据块、提交、放弃、进度 |
| 9–12 | 应用管理 | 激活、切换、回滚、删除 |
| 13–14 | 应用 GATT 特征读写 | ABI 2.1 |
| 15–17 | 宿主图片命令 | 兼容路径 |
| 18–23 | 配对与 BondAuth | 见 [配对 v2](ble-pairing-v2.md) |
| 24–27 | 通用消息通道 | 见 [ABI 2.3 消息通道](abi-2-3-channel.md) |
| 28 | 只读诊断命令 | 见下文 |

op 2–17 和 op 24–28 都要求已鉴权，按上一节的签名格式收发。下面各表只列 `payload` 部分。

### op 2：Discovery

| 请求 | 成功回复 |
|---|---|
| 空 | 20 字节发现记录 |

### op 3：状态

| 请求 | 成功回复 |
|---|---|
| `offset:u32` | `total:u32, offset:u32, JSON 片段`（最多 400 字节） |

`offset=0` 时宿主取一份当前状态的快照，后续页都读这份快照，分页期间状态变化不会让内容错位。客户端从 0 开始，每次用已拼好的长度作为下一页的 `offset`，直到拼满 `total` 字节。`offset` 大于等于 `total` 时返回 5。字段含义见 [状态 JSON](#状态-json)。

### op 4–8：上传应用包

| op | 命令 | 请求 | 成功回复 |
|---:|---|---|---|
| 4 | BEGIN | `size:u32, sha256:32, app_id:32` | 已收到的偏移 `u32` |
| 5 | CHUNK | `offset:u32, data`（至少 1 字节） | 已收到的偏移 `u32` |
| 6 | COMMIT | 空 | 空 |
| 7 | ABORT | 空 | 空 |
| 8 | PROGRESS | 空 | `received:u32, total:u32` |

1. **BEGIN**：`size` 是整包长度，4148（4096 字节包头加最小 52 字节 ELF）到 1048576 字节；`sha256` 是整包的 SHA-256。宿主取得准入锁，在 PSRAM 里分配整包缓冲，拿不到准入锁返回 3。已有上传时，参数完全相同的 BEGIN 返回当前偏移，参数不同返回 8。
2. **CHUNK**：`offset` 不能超过已收到的长度。落在已收范围内、内容一致的重复块被接受；内容不一致或有空洞返回 5。
3. **COMMIT**：要求已收齐，否则返回 8。宿主先核对整包 SHA-256（不符返回 6），再交给安装器检查包头、载荷 SHA-256、ELF、清单、导入表和应用 ID（拒绝返回 9），然后写进该应用的备用槽，成为 `pending`。提交只暂存，不激活。
4. **ABORT**：丢弃缓冲，释放准入锁。

上传期间一直占着准入锁，按键和其他管理命令会得到 3。COMMIT、ABORT、断开连接和会话过期都会释放缓冲和准入锁。包格式和写槽顺序见 [应用目录与槽位](app-catalog.md)。

### op 9–12：应用管理

| op | 命令 | 请求 | 成功回复 |
|---:|---|---|---|
| 9 | ACTIVATE | `app_id:32` | 空 |
| 10 | SWITCH | `app_id:32` | 空 |
| 11 | ROLLBACK | `app_id:32` | 空 |
| 12 | REMOVE | `app_id:32` | 空 |

宿主取得准入锁后把命令交给 App Manager，执行完才回复。同一会话里还有上传在进行时返回 3。宿主内部错误映射为：状态不允许或应用不存在 → 8，内存不足 → 7，参数无效 → 4，其他失败 → 10。各命令对目录的影响见 [应用目录与槽位](app-catalog.md#管理命令)。

### op 13–14：应用 GATT 特征

前台应用用 ABI 2.1 注册了自定义 GATT 服务时，客户端用这两个命令做带签名的读写。

| op | 命令 | 请求 | 成功回复 |
|---:|---|---|---|
| 13 | APP_READ | `generation:u32, revision:u32, index:u32` | 特征当前值，最多 128 字节 |
| 14 | APP_WRITE | 同样 12 字节定位，后跟 0–128 字节值 | 空 |

- `generation`、`revision` 取自状态里的 `generation` 和 `ble_service_revision`，`index` 是特征下标 0–3。应用重新加载或重新注册服务后，旧的定位失效。
- APP_WRITE 在应用处理完对应的 BLE 写入事件后才回复。
- 两个命令都要取得准入锁。
- 应用自定义特征也可以直接写：写入内容必须是同样分片格式的签名 op 14 帧，并且 `index` 等于这个特征的下标；回复仍从 `7b9d0005` 读。
- 直接 GATT 读和通知要求连接已鉴权，但值本身不带签名。需要可信的完整值时用 op 13。通知长度不超过 `ATT_MTU - 3`。

### op 15–17：宿主图片命令

这是宿主自带的一条 PNG 显示路径，复用 op 5–8 传数据，最后由 op 6 触发显示。宿主把 PNG 作为 INPUT 事件交给前台应用，要求应用是 ABI 1，或在清单里声明了 `APP_INPUT_PNG`。现有客户端都通过 photoframe 自己的图片事务推图（见 [蓝牙通信](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/06-bluetooth.md#7-图片事务)），这条路径只为兼容保留。

| op | 命令 | 请求 | 成功回复 |
|---:|---|---|---|
| 15 | IMAGE_CAPS | 空 | `minor:u8=1, boot_id:16, max_png:u32, receipt_capacity:u8=4` |
| 16 | IMAGE_BEGIN | `boot_id:16, request_id:16, size:u32, sha256:32, app_id:32` | `received:u32, state:u8, result:u16` |
| 17 | IMAGE_RESULT | `boot_id:16, request_id:16` | `state:u8, result:u16, size:u32, sha256:32, app_id:32` |

- `boot_id` 是每次开机随机生成的 16 字节。请求里的 `boot_id` 与当前不符时返回 14。
- `request_id` 是非零的随机 128 位值，每次推图意图一个。
- PNG 长度 8 字节到 5 MiB。目标应用不是当前前台应用、或不接收 PNG 时返回 8。
- `state`：0 没有记录（在 BEGIN 回复里表示接收中）、1 正在执行、2 已完成。只有 `state=2` 且 `result=0` 证明显示完成。查不到的 ID 在 IMAGE_RESULT 里返回全零。
- op 6 提交图片时，宿主先记下请求 ID、摘要、长度和目标应用，再同步调用显示，屏幕刷新完成并断电后才回复。
- 宿主在内存里保留最近 4 份回执，重启后清空。用已有回执的请求 ID 重发相同参数的 IMAGE_BEGIN，直接返回已有结果，不会再刷一次；参数不同返回 8。

### op 28：只读诊断命令

已鉴权的主人可以执行一组只读命令查看宿主状态。命令不修改任何状态、不重启设备，输出里不含会话密钥、配对根密钥和 Wi-Fi 密码。

| 请求 | 成功回复 |
|---|---|
| `offset:u32`，`offset=0` 时后跟命令文本 | `total:u32, offset:u32, exit:u8, flags:u8, 输出片段`（最多 400 字节） |

- `offset=0`：命令文本 1–256 字节，必须是合法 UTF-8，除空格和制表符外不含控制字符。宿主按空白拆成最多 16 个参数，**执行一次**，保留最多 4095 字节输出，并返回第一页。输出被截断时 `flags` 第 0 位为 1。
- `offset>0`：不带命令文本，读取本会话最近一次输出的对应页，不会重新执行。新连接或新会话没有可读的输出。
- `exit`：0 成功、1 命令出错、2 用法错误（空命令、参数过多或多余）、127 未知命令。`exit` 非零时 `code` 仍为 0。
- `total` 可以为 0，这时回复只有 10 字节头。
- 错误：负载短于 4 字节、命令文本非法、`offset>0` 却带了命令 → 4；`offset>=total` 或之前没有输出 → 5；固件没有接入诊断命令 → 8。不认识 op 28 的旧固件返回 4，客户端可以据此判断是否支持。

| 命令 | 输出 |
|---|---|
| `help` | 命令列表 |
| `uname`、`version` | 宿主项目名、版本、构建时间、支持的应用 ABI、ESP-IDF 版本、芯片版本和核数 |
| `uptime` | 开机时长和复位原因编号 |
| `free` | 内部 RAM 和 PSRAM 的总量、空闲、最低空闲、最大块 |
| `apps` | 每个应用的 bank、ID、当前版本、槽位、ABI，以及前台、选中、试运行、待激活标记 |
| `ps`、`current` | 前台应用、槽位和版本、ABI、App Manager 阶段、就绪和确认标志、generation、最近一次切换 |
| `status` | op 3 的状态 JSON |
| `wifi` | Wi-Fi 状态和 IPv4，不含 SSID 和密码 |

诊断命令在传输工作线程上执行，只读 App Manager 已发布的状态快照和堆计数，不占准入锁，所以上传期间也能用。

## 状态 JSON

op 3、诊断命令 `status` 和各客户端的状态页显示的是同一份 JSON。

**当前前台应用**

| 字段 | 含义 |
|---|---|
| `loaded_app` | 前台应用所在的 bank，-1 表示没有加载应用 |
| `loaded`、`version` | 前台应用运行的槽（0 是 A，1 是 B）和发布版本 |
| `active`、`previous`、`pending`、`trial` | 前台应用在目录里的四个槽位字段 |
| `payload_abi`、`payload_abi_minor` | 前台应用的 ABI 主、次版本 |
| `abi` | 固定为 1，是旧管理接口的兼容字段，不代表应用 ABI |
| `ready` | ELF 已加载 |
| `runtime_ready` | START 已成功，应用可以接收事件 |
| `confirmed` | 已就绪，并且没有应用在试运行 |
| `generation` | 加载代次，每加载一次应用加 1 |
| `display_completed` | 最近一次应用事件里显示是否成功。不画屏的事件会把它清成 `false` |
| `cache_bytes` | 所有应用缓存占用的字节数 |

**目录**

| 字段 | 含义 |
|---|---|
| `selected` | 开机加载的应用 bank |
| `trial_app` | 正在试运行或切换中的应用 bank，-1 表示没有 |
| `capacity` | 最多可安装的应用数，固定为 5 |
| `apps[]` | 每个已安装应用一项，见下表 |

`apps[]` 每项：

| 字段 | 含义 |
|---|---|
| `id`、`name` | 应用 ID 和清单里的显示名（没有清单时等于 ID） |
| `bank` | 目录位置 0–4 |
| `slot_bytes` | 槽大小，1048576 |
| `active`、`previous`、`pending`、`trial` | 槽位字段，含义见 [应用目录与槽位](app-catalog.md#目录) |
| `versions` | A、B 两槽里通过校验的包的发布版本，无效或空槽为 `null` |
| `abi`、`inputs` | `active` 版本的 ABI 主版本和清单 `inputs` |
| `legacy_push_only` | `active` 版本是 ABI 1 |
| `button_eligible` | 功能键短按能切到它（有 `active` 版本且是 ABI 2） |

**App Manager**

| 字段 | 含义 |
|---|---|
| `phase` | 最近一次工作的阶段：`starting`、`idle`、`fault`、`display`、`switching`、`management`、`pairing`、`pairing_reset`、`network`、`timer`、`ble`、`coc`、`channel` |
| `last_result` | 最近一次操作的结果码 |
| `last_switch_source`、`last_switch_target` | 最近一次切换的来源（`boot`、`ble`、`button`）和目标应用 ID |
| `ignored_buttons` | 因为忙或状态变化被忽略的短按次数 |
| `runtime_diagnostic` | 启动诊断，形如 `reset=1 previous=ready current=ready guard=complete_journal`，见 [启动诊断](app-catalog.md#启动诊断) |

`phase` 为 `fault` 表示没有应用在运行，但仍可以通过蓝牙管理。这时 Wi-Fi 变化不会调用应用，`last_result` 保留启动失败时的结果码。

**应用通信接口**

| 字段 | 含义 |
|---|---|
| `ble_service_uuid`、`ble_service_revision`、`ble_characteristics` | 前台应用注册的 GATT 服务 UUID（32 位十六进制，按书写顺序）、注册修订号和特征数。没有注册时分别为空串、0、0 |
| `coc_version`、`coc_psm`、`coc_state`、`coc_rx_payload_max` | 前台应用的 CoC 通道。打开时 `coc_version=1`、`coc_psm=129`（`0x81`）；`coc_state` 0 未打开、1 等待连接、2 已连接 |

**系统**

| 字段 | 含义 |
|---|---|
| `uptime_ms` | 开机到现在的毫秒数 |
| `reset_reason` | ESP-IDF 复位原因编号：1 上电、3 软件复位、4 异常、5 中断看门狗、6 任务看门狗、9 欠压、11 USB 复位、14 电源毛刺、15 CPU 锁死 |
| `free_internal_ram`、`minimum_free_internal_ram` | 内部 RAM 当前空闲和本次开机最低空闲 |
| `free_psram`、`minimum_free_psram` | PSRAM 当前空闲和本次开机最低空闲 |

`uptime_ms` 比上次读到的小，说明中间重启过；结合 `reset_reason` 可以区分“蓝牙断了”和“设备重启了”。

## 并发与准入

宿主里所有会碰 Flash、应用代码或屏幕的工作，都由唯一的 App Manager 任务串行执行。蓝牙回调只复制数据，长操作不会阻塞 NimBLE 任务。

- 上传（op 4）、管理命令（op 9–12）、应用特征（op 13–14）、宿主图片（op 16）、通用消息（op 24–27）都要先取得准入锁。功能键、定时器和网络事件也用同一把锁，拿不到就返回 3。
- 每个排队的作业都带着发起时的授权代次。长按功能键重置配对后，旧主人排队的作业在执行前会被拒绝。
- 连接断开不会打断已经开始的显示，借给显示的数据在显示结束后才释放。
- 宿主用连接编号区分前后两个连接，旧连接的结果不会发给新连接。

## 内存与构建配置

`sdkconfig.defaults` 里的蓝牙相关配置：

| 配置 | 值 |
|---|---|
| 角色 | 只启用外设，最多 1 个连接 |
| ATT MTU | 首选 247 |
| 动态 GATT 服务 | 启用，供应用自定义服务使用 |
| L2CAP CoC | 最多 1 个通道 |
| NimBLE 内存 | 主机分配走 PSRAM |
| Wi-Fi 与蓝牙共存 | 软件共存 |
| 蓝牙 SMP 安全 | 关闭。鉴权在应用层完成，链路不加密 |

安装包缓冲（最多 1 MiB）和宿主图片缓冲（最多 5 MiB）都在 PSRAM 里分配，分配失败返回 7。两者共用准入锁，不会同时存在。

## 测试

协议的 C 实现由主机上的原生测试覆盖，使用 OpenSSL 提供 SHA-256 和 HMAC，开启 ASan 和 UBSan：

| 测试 | 覆盖 |
|---|---|
| `tests/ble/test_protocol.c` | 签名、序号、重放、分片、上传、会话过期、宿主图片命令 |
| `tests/ble/test_shell.c` | op 28 的校验、单次执行和分页 |
| `tests/ble/test_pairing.c`、`tests/ble/pairing-v2.json` | 配对 v2 状态机和固定测试向量 |

这些测试由 `tools/test_ble_protocol.py` 和 `tools/test_pairing.py` 编译运行，运行方法见 [README](../README.md#测试)。电脑端的 Go 实现用同一组向量做对照测试。
