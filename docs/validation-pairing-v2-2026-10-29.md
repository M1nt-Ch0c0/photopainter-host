# 2026-10-29 物理配对 v2 验证

**尚未实机验证。** 本页记录源码与构建证据；实际刷写、按键和实体显示证据由主流程追加，
不能从这里推断设备已经更新。测试时源码为基线 `be664989697e` 加本轮改动；
最终 main 提交及提交后重建产物见[早上验收清单](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/MORNING-ACCEPTANCE.md)和外部 manifest。

## 实现范围

- P-256 双方 commitment/reveal、HKDF-SHA256、六位短认证码。
- 首个带 root MAC 的 Poll 表示客户端已保存候选，之后才显示可确认画面。
- 通过唯一 Manager 及既有板级显示显示中文配对提示。完整刷新返回成功后
  才开放物理短按；批准先提交 NVS，再报告 bound。
- owner 单 blob 存储包含 CID/BID/root/显示名和版本/摘要。首次 namespace
  不存在或有效 unbound tombstone 才是未绑定；已有 namespace 缺 owner、
  损坏记录、存储未初始化或提交失败均不允许新绑定。
- GPIO 4 功能键长按 8 秒并松开锁存重置，撤销 GATT/CoC 会话和排队事件，
  不改 Wi-Fi/已安装应用。GPIO 0 BOOT 与 GPIO 5 电源键不参与。
- BLE 固定 token op 1 拒绝；HTTP 控制入口 410，带旧 token 同样不能操作。
- APP 仍用 ABI 2.2；本功能不需要变更 SDK 副本或重装 photoframe v20。

## 已完成离线验证

今晚最终检查在 IDF Python 环境下执行，设置 `IDF_PATH` 和已有 v20
`PHOTOFRAME_TEST_ELF` 后：**90 项全部通过，0 跳过**。额外集成项只生成
临时测试 NVS 镜像或读取 APP ELF，没有打开串口、蓝牙或访问真实设备。
早前未设置这两个环境值时为 89 项、2 可选跳过；不能把旧计数当最终结果。

```sh
export IDF_PATH=/absolute/path/to/pinned-esp-idf
export PHOTOFRAME_TEST_ELF=/absolute/path/to/photoframe.app.elf
python -m unittest discover -s tools -p 'test_*.py' -v
```

未提供 `PHOTOFRAME_TEST_ELF` 时跳过实际 ELF 集成检查；未提供 `IDF_PATH` 时
跳过 NVS 生成器集成检查。今晚两项均已执行。新增 provisioning 回归证明
默认 v2 不要求/写入旧 token，明确 `--legacy-token` 才生成 v1 字段，两种
生成文件均为 0600；整个 NVS 替换会清除配对的行为已写入维护说明。

原有两个 ABI 2.2 测试缺口（service 测试漏链 CoC、manifest 未知
minor 仍使用 2）已补齐；Wi-Fi 仲裁测试现在断言旧 token 完全不被加载。

配对测试运行真实生产 C 状态机，使用真实 OpenSSL P-256/HMAC/SHA256，
ASan/UBSan 开启。固定 JSON 向量独立来自 Node OpenSSL ECDH/HKDF，和客户端
Go/WebCrypto 共用期望：SAS `314164`，完整 transcript 316 字节。

覆盖 commitment/nonce 被替换、非法曲线公钥、root/session/commit 固定向量、
client ready 前短按、画面完成前短按、错 screen serial、显示失败、NVS 失败、
确认重复、重启加载 owner、超时、带证明取消、公开 BID 不能取消、换连接
重放 Poll、解绑后旧 owner 拒绝、损坏存储失败关闭。

真实按键纯策略测试覆盖 8 秒长按、定时事件 epoch 变化、忙时开始/松开、
启动一直按住与 debounce。真实 Manager pthread 测试覆盖 admission 内画面
调用、配对期间暂缓 APP 网络事件并在结束后恢复。真实 HTTP handler 注册
测试覆盖 7 个路由及带/不带旧 Authorization 的 410/health 行为。

中文配对图由真实 `pairing_screen.c` 生成，像素全部是设备支持的黑/白/蓝。
预览 `/tmp/photopainter-pairing-preview/prompt.png` 已目视检查，临时路径不是
长期产物存储。固定文字是一位位图，未嵌入字体文件；生成器在 tools 下。

## 构建

固定 ESP-IDF `5e6f53cdb31fe5708eae3f55af9737be2822db22` 与未修改的 Registry
elf_loader 1.3.3，`idf.py -B build-host build` 通过。

- `build-host/photopainter_host.bin`：1,132,800 字节。
- SHA-256：`2f9f221766c9b3e308c0be34ac7d49ad79c9629b137ce38b9726113e644751d1`。
- factory 分区 5 MiB，产物检查通过；未改分区布局。

该摘要为本轮未提交工作区下的构建记录。最终提交后若 Git 派生版本字段
变化，重建产物的摘要也会变化；早上应使用交付清单中与最终提交对应的
产物 manifest，不能混用早期候选。今晚未刷写设备或改变运行服务。

## 独立审阅后的撤权与存储修复

独立审阅发现并已关闭三项部署阻断：远端上传或连续刷新导致长按无法
撤权、旧局部请求跨 reset 后仍执行副作用、NVS 下层 CRC 错误以 NOT_FOUND
表现时被当成未绑定。最终实现使用物理显示/重置请求的原子控制锁、请求
serial 与 transport owner-thread drain/ACK、manager 排队任务授权 epoch
复验，以及显式 unbound tombstone。已有 namespace 缺 owner 一律失败关闭。

回归实际构造“旧授权先检查、reset 完成后才取得 admission”的交错，证明
SELECT/STAGE/REMOVE/ROLLBACK/RENDER/APP-write 均不执行；排队旧任务先取消，
worker 归还 borrowed job 与 admission 后的 exact ACK 才能触发 NVS reset。
上传或连续显示 busy 都允许长按锁存请求：已开始的实体刷新与借用输入
完整结束前不提交 NVS reset，请求锁存后第二次 display_begin 始终被拒绝。
真实存储 loader 桩复现 NVS CRC 擦条目后 NOT_FOUND，
验证仍为 fault；reset 写有效 tombstone 并重新加载为 unbound。

最后全量验证还修正了旧 Wi-Fi HTTP 测试服务器未消费 POST 体造成的偶发
TCP RST，生产旧 HTTP 退役策略不变。独立审阅已复读最终实现并关闭阻断。

连续刷新场景另以三阶段 manager 回归验证：正在刷新时锁存请求并可先取得
空闲 transport 的 ACK，但 NVS reset 不执行；硬件刷新完成后仍暂扣借用
job，新的 display_begin 因 pending 被拒绝，NVS 仍不执行；最后 borrowed
job 返回并释放 admission 后才持久清除 owner。90 项测试及本页最终构建
均包含这个修复，早期构建摘要不得用于本次部署。

本开发路未打开串口、未刷写、未发送业务图片。物理确认、NVS 真机重启持久化、
长按撤销旧 owner、浏览器再次连接、实际图片回执仍须完整链路验收。
