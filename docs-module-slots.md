# 独立应用包、A/B 与安装管理

本页是当前五 bank 框架的管理指南，覆盖 ABI 1/2.0/2.1/2.2。旧单应用 ABI 1 部署说明保留在 [历史提交](https://github.com/M1nt-Ch0c0/photopainter-host/blob/3b6f7e2/docs-module-slots.md)，不作为当前分区写入教程。

当前源码使用物理配对 v2，**尚未实机验证**。包格式、APP ABI 和分区不变；管理使用已配对 BLE，旧 HTTP token 控制入口返回 410。

## 包与身份

每 APP 固定占用一个 bank，A/B 各 1 MiB，最大五个 APP。包包括 4096 字节头和 ELF payload，总计不超过 1 MiB。format 1 仅保留旧 photoframe ABI 1；format 2 携带 32 字节 NUL 填充 APP ID，可用于 ABI 1/2。

头部记录 magic `0x31464c45`、格式、ABI 主版本、长度、uint32 发布版本和 payload SHA-256。format 2 在偏移 52 增加 APP ID。ABI 2 的 ELF 另含只读 manifest：身份必须与包头和安装目标一致，导入必须在 SDK 白名单内，GATT导入要求2.1+，CoC导入要求2.2。未知布局、能力或导入拒绝，不手工修改包头绕过校验。

发布版本是操作者分配的标签，不是反降级计数器；SHA-256 是完整性校验，不是作者签名。APP 是受信任原生代码，不是沙箱。

## 构建和打包

在固定 IDF 提交 `5e6f53cdb31fe5708eae3f55af9737be2822db22` 与 Registry elf_loader 1.3.3 下独立构建 APP。最小工程见 [Wiki 快速开始](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Quickstart.md)。

```sh
# photoframe/examples/lifecycle 中
idf.py -B build-app -DIDF_TARGET=esp32s3 build
# photopainter-host 中
python3 tools/module.py package --app lifecycle --version 1 \
  --input ../photoframe/examples/lifecycle/build-app/lifecycle.app.elf \
  --output /tmp/lifecycle-v1.pkg
```

普通构建生成的固件 bin 不是业务 APP 部署产物。宿主不嵌入业务 ELF；ABI 兼容更新无需重建宿主。

## 状态与唯一安装路径

| 字段/操作 | 含义 |
|---|---|
| active / previous | 当前确认版本 / 可用回退版本，0=A、1=B、-1=无 |
| pending / trial | 已暂存候选 / 未确认试运行 |
| selected / trial_app / loaded_app | bank 索引，不是版本槽 |
| ready / runtime_ready / confirmed | 已加载 / 满足运行条件 / 已确认且无全局 trial |
| payload_abi | 运行 APP 的 ABI，顶层 abi=1 是兼容管理封套 |
| stage/install | 写备用槽并登记候选，不执行 APP |
| activate | 显式试运行 pending，先持久化 trial |
| switch | 切到已确认 active，不激活该 APP 的 pending |
| rollback | 取消候选或恢复 previous |
| remove | 删除非当前 APP 目录项，不是安全擦除 |

已配对 BLE 的上传/管理通过授权代次检查进入唯一 App Manager 和既有 stage 状态机。校验通过后，先使备用旧 fallback 标记失效，再写 payload、读回校验，最后提交包头和 pending；活动槽保持可用。

## 应用 OTA

停止竞争的自动客户端，先通过 `ble pair` 和设备功能键建立关系，再核对真实目录和授权。后续命令使用保存的关系，不需要设备令牌或 IP：

```sh
ai-quota-frame ble status
ai-quota-frame ble inspect --file /tmp/lifecycle-v1.pkg
ai-quota-frame ble install --file /tmp/lifecycle-v1.pkg
ai-quota-frame ble activate --app lifecycle
ai-quota-frame ble status
```

v2 的 `/api/module` GET/POST 都返回 410，不暂存或激活。`tools/module.py package` 仍是当前本地打包工具；其 HTTP 子命令仅供明确的 v1 历史维护，不应对 v2 使用。BLE 管理错误和恢复规则见 [协议](docs/ble-protocol.md)。

## 确认与恢复

ABI 2 在成功 START 报告 APP_READY，并完成声明所需的真实首屏后确认。无显示 APP 可不刷新面板；ABI 1 保留首次成功兼容输入确认。不要让框架管理流程强制所有 APP 接收 PNG。应用业务输入见各 APP 文档。

未确认前复位或激活失败，恢复此前确认版本/选择。损坏目录不擦除或猜测恢复。开机 active 失败只尝试 previous 一次，后者也失败保持可管理故障空闲，避免无限重启/刷屏。完整状态机与布局见 [多应用指南](docs-multi-apps.md) 和 [运行层](docs-runtime-v2.md)。

管理响应丢失时重连核对 APP、bank、槽和发布版本，不盲目重复激活。显示结果另由 APP 协议确认，暂存成功不是物理完成。回退到旧 ABI 1 时要按旧 APP 的输入合同完成确认。

## 宿主维护与历史迁移

当前五 bank 分区表保持不变；应用 OTA 不写 factory。没有宿主整机 OTA。另获授权更新宿主时，先备份全部 16 MiB 并校验 SHA-256，有 SD 按要求备份，然后只写 `0x10000` 的宿主 bin，保留 NVS、目录和应用槽。

全新设备种子安装或旧两槽布局迁移必须单独审查实际分区与恢复计划，不能照抄历史固定地址。本轮已按授权更新宿主factory；分区迁移、首次provision仍是独立流程，普通升级保留已有配置。

## 验证

```sh
python3 -m unittest discover -s tools -p 'test_*.py'
PHOTOFRAME_TEST_ELF=/absolute/path/app.elf \
  python3 -m unittest discover -s tools -p 'test_*.py'
```

测试覆盖包/ELF/manifest、写入中断、确认和回退、manager 与启动恢复。真实测试范围分别见 [运行层记录](docs/validation-runtime-v2.md)、[BLE 记录](docs/validation-ble.md)、[Mac 离线记录](docs/validation-ble-images.md)。历史验证按当时版本解释，不等同当前所有功能均已部署。
