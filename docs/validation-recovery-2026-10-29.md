# 2026-10-29 photoframe 故障空闲恢复

时间均为 UTC+8。源码基线：宿主 `be664989697e`、photoframe
`ab2429a32ae3`、电脑后台 `513fc5e54b8c`。本次未写入固件、应用包或
Wi-Fi 配置，未打开 USB 串口，也未重启设备或后台服务。

## 操作与结果

先通过现有后台的唯一设备队列暂停后续自动推图，确认没有活动操作和
待确认图片。所有设备查询均使用后台既有凭据完成双向鉴权；凭据未输出。

00:10:27 签名状态显示：

- `phase=fault`，`loaded_app=-1`，`runtime_ready=false`，`generation=0`。
- photoframe 两槽均为 v20，active=1，previous=-1，pending=-1。
- `uptime_ms=414679`，`reset_reason=1`。

00:10:48 只发送一次 `console switch photoframe`。后台未及时读到切换后的
状态，报告“操作结果暂不确定”。没有重发 switch；随后仅执行签名状态查询。

00:11:41 的重新鉴权状态确认：

- `phase=idle`，photoframe v20 / slot 1，`runtime_ready=true`、`confirmed=true`。
- ABI 2.2，`generation=1`，两个 GATT 特征已注册。
- CoC `version=1`、`state=1`、PSM=129，RX 应用记录上限 4058 字节。
- `uptime_ms=489019`，`reset_reason=1`，没有发生本轮设备重启。

这证明原已安装 v20 可以加载运行，显式选择已恢复应用。ABI 2 START 的
宿主合同要求 APP_READY 及所声明的首屏显示完成后才进入上述就绪状态。
后续 TIMER 会更新 `display_completed`，因此它不是持久的首屏完成回执。
本次恢复未发送业务图片。用户随后确认首屏已经显示，方向和颜色正常；
该目视确认只覆盖这次恢复首屏，不代替后续业务图片的完成回执。

00:13:53 的鉴权发现信息仍为 Wi-Fi `state=1`（连接中）、IP `0.0.0.0`。
应用恢复没有修复或证明 Wi-Fi 联网。

## 根因边界与诊断修正

`generation=0` 表明故障发生在首次成功加载之前；显式选择同一 v20 后
无需重启即恢复，最符合持久启动保护阻止自动重入的路径。由于本次未读取
Flash/NVS 或启动保护记录，不能断言具体 journal phase、最初失败原因，
也不能把 POWERON 复位原因归因于电池、USB 或软件。

另外确认了独立的诊断缺陷：在故障空闲、没有可运行 APP 时，Wi-Fi
重连产生的 NETWORK 事件仍会调用 `app_runtime_event()`，返回
`APP_ERR_UNSUPPORTED=-9`，覆盖真实的启动错误。因此本次初始 `last_result=-9`
不能解释为已证明 APP 或 ABI 不支持。

源码修正保留网络连接状态更新，仅在 `app_runtime_ready()` 时投递 APP
NETWORK 事件，避免覆盖启动故障；既有 `last_result` 的实际操作结果含义不变。
该修正尚未刷入设备。

## 验证

- 针对现有 manager 原生回归：启动失败后触发网络事件，确认连接状态仍更新，
  APP 回调未执行，`phase=fault` 和原启动错误保留；再显式选择恢复运行，
  继续验证已有串行、事件合并和输入生命周期行为。单项通过。
- 固定 ESP-IDF `5e6f53cdb31fe5708eae3f55af9737be2822db22`、Registry
  elf_loader 1.3.3 的宿主构建通过，产物 `build-host/photopainter_host.bin`。
- 未运行全面回归，未刷宿主。设备恢复使用的是原已部署代码。

操作前后非秘密诊断快照暂存于本机
`/tmp/photopainter-recovery-20261029/before.json` 和 `after.json`；该临时目录
不是长期恢复材料。自动推图暂停/恢复由本轮主流程统一协调。
