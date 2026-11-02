# ABI 2.2 CoC 通道规格

这一页是宿主 L2CAP CoC 通道（ABI 2.2）的线上格式和宿主侧行为：客户端怎样打开通道、hello 消息和数据记录的字节格式、方向密钥怎样派生、宿主怎样做流控和清理。应用一侧的 C 接口（`app_host_coc_*_v22`、配置和事件结构、返回码）见 [应用接口参考](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/05-app-api-reference.md#coc-通道abi-22)；CoC 和另外两种通信方式的对比见 [蓝牙通信](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/06-bluetooth.md#8-应用通信的三种方式)。

L2CAP CoC（面向连接的信道）是蓝牙上比 GATT 快得多的数据通道。当前只有 macOS 原生客户端使用它推图。

## 归属

- 全局只有一个 CoC 注册和一个 CoC 连接，归当前前台应用独占。应用用 `APP_MANIFEST_COC` 声明 ABI 2.2，并调用 `app_host_coc_open_v22()` 打开通道。
- 注册跟随应用的加载代次（generation）。切换应用、卸载或应用调用 `close` 时，宿主撤销注册并释放队列。
- 应用只看到完整的“记录”，宿主负责鉴权封套、分帧和流控。应用拿不到任何密钥。

实现位置：

| 文件 | 内容 |
|---|---|
| `main/app_coc.c` | 注册、收发队列、会话编号、事件合并 |
| `main/ble_coc.c` | NimBLE L2CAP 服务端、hello、记录封套、流控 |
| `main/app_services.c` | 应用调用时机检查 |
| `main/app_manager.c` | 把 COC 事件串行派发给应用 |

## 打开通道

1. 客户端在 GATT 上完成 BondAuth（见 [配对 v2](ble-pairing-v2.md#bondauthop-23)），得到会话密钥 `K`。
2. 客户端用 op 3 读状态，确认 `coc_version=1`，取得 `coc_psm`（固定为 `0x81`）和 `generation`。
3. 客户端在**同一个 GAP 连接**上打开 PSM `0x81` 的 CoC 通道。宿主只接受已经鉴权的连接，对端声明的 SDU 至少 128 字节，同时只接受一条通道。
4. 宿主先发一条 82 字节的 hello，双方从中派生方向密钥。之后双方收发数据记录。

宿主侧的 L2CAP MTU 是 4096 字节。CoreBluetooth 等平台把通道暴露成字节流，所以宿主按长度字段增量解析，不假设一次读写等于一条 SDU。

## hello

宿主在通道建立后发送：

| 偏移 | 字节 | 内容 |
|---:|---:|---|
| 0 | 2 | 后续长度，固定为 80 |
| 2 | 4 | ASCII `PPC1` |
| 6 | 4 | 应用 generation |
| 10 | 4 | CoC 会话编号，非零 |
| 14 | 2 | 宿主能接收的最大应用记录长度 |
| 16 | 2 | 宿主会发送的最大应用记录长度 |
| 18 | 32 | 随机数 `nonce` |
| 50 | 32 | `HMAC(K, "pp-coc-v1/hello" || hello[2:50])` |

- 接收上限等于应用配置的 `rx_sdu_bytes` 减 38。
- 发送上限等于对端声明的 MTU 减 38，最多 4058。

客户端核对 hello 的 HMAC、generation 后，双方派生：

```
c2s_key = HMAC(K, "pp-coc-v1/c2s" || nonce)    // 客户端 → 宿主
s2c_key = HMAC(K, "pp-coc-v1/s2c" || nonce)    // 宿主 → 客户端
```

## 数据记录

hello 之后，双方的每条记录：

```
length:u16 | sequence:u32 | payload | tag:32
```

| 字段 | 规则 |
|---|---|
| `length` | 不含自身的长度，等于 4 + payload 长度 + 32 |
| `sequence` | 每个方向独立，从 1 开始，每条加 1，不能回绕 |
| `payload` | 应用记录，至少 1 字节，不超过对方在 hello 里给出的上限 |
| `tag` | `HMAC(方向密钥, sequence || payload)` |

每条记录的封套开销是 38 字节，4096 字节的记录最多带 4058 字节应用数据。长度、序号或标签不对时，宿主断开通道。

记录提供身份认证和完整性，内容不加密。

## 流控与生命周期

| 情况 | 宿主的行为 |
|---|---|
| 接收队列满 | 不再给 L2CAP 提供接收缓冲，对端因此停下；应用取走记录后恢复 |
| 发送信用不足 | 等待 NimBLE 的 `TX_UNSTALLED` 事件后继续，已交给 NimBLE 的记录不会重发 |
| 每次处理 | 每个无线事件最多发送 4 条记录 |
| 空闲 | 60 秒没有收发、并且 App Manager 没有在执行作业时，宿主断开通道 |
| 连接期间 | GATT 会话的空闲计时保持活跃；会话 2 小时的总时长仍然有效 |
| 长按重置配对 | 立即撤销 CoC 授权，丢弃队列 |
| 对端断开 | 清空队列，应用收到 `APP_COC_DISCONNECTED`；注册保留，下一个客户端可以直接连 |
| 应用 `close`、切换、卸载 | 撤销注册，释放队列，旧会话编号失效 |

宿主为应用的队列分配 `rx_slots × rx_sdu_bytes + tx_slots × 4096` 字节，另外有 NimBLE 内存池和协议缓冲。宿主不解析记录内容。

## 版本兼容

- CoC 的导入函数只允许声明了次版本 ≥ 2 的应用使用，打包工具和宿主都会检查。
- 不支持 2.2 的旧宿主会在安装时拒绝 2.2 应用，所以要先升级宿主固件，再安装需要 CoC 的应用。
- 宿主固件通过 USB 更新，见 [维护与排错](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/08-maintenance.md#用-usb-更新宿主固件)。
