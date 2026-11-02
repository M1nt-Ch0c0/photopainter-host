# 应用目录与槽位

这一页是宿主管理已安装应用的精确规则：Flash 里的槽位地址、安装包格式、目录在 NVS 里怎么存、每个管理命令怎样改目录、开机时启动保护记录的格式和诊断字符串、功能键的判定。改 `main/module_slots.c`、`main/app_boot.c`、`main/photoframe_plugin.c` 或排查槽位问题时查这一页。

A/B 槽、试运行、确认和启动保护的原理与示例见 [设备是怎么工作的](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/03-how-the-device-works.md#应用目录与-ab-槽)。客户端怎样上传和发命令见 [宿主蓝牙线上协议](ble-protocol.md#op-48上传应用包)。

## 分区与槽位地址

分区表在 `partitions.csv`，Flash 共 16 MiB：

| 分区 | 类型/子类型 | 偏移 | 大小 | 内容 |
|---|---|---|---|---|
| `nvs` | data/nvs | `0x9000` | `0x6000` | Wi-Fi 配置（命名空间 `photo`）、配对主人（命名空间 `pp_pair`） |
| `phy_init` | data/phy | `0xf000` | `0x1000` | 射频校准 |
| `factory` | app/factory | `0x10000` | `0x500000` | 宿主固件 |
| `elf_a` | data/`0x40` | `0x510000` | `0x100000` | bank 0 的 A 槽 |
| `elf_b` | data/`0x41` | `0x610000` | `0x100000` | bank 0 的 B 槽 |
| `elf_state` | data/nvs | `0x710000` | `0x6000` | 应用目录（命名空间 `module`）和启动记录（命名空间 `runtime`） |
| `app_slots` | data/`0x42` | `0x720000` | `0x800000` | bank 1–4 的 A、B 槽 |

- 最多 5 个应用（`APP_LIMIT`），每个应用固定占一个 bank，每个 bank 有 A（0）、B（1）两个 1 MiB 槽。
- bank 0 用 `elf_a`、`elf_b`；bank `i`（1–4）的槽 `s` 在 `app_slots` 分区内的偏移是 `((i - 1) × 2 + s) × 0x100000`，绝对地址最高到 `0xf20000`。
- 宿主启动时检查这三个数据分区存在且大小正确，不对就不加载任何应用。

## 安装包格式

`tools/module.py package` 生成 `.pkg`：4096 字节包头，后面紧跟 ELF。

| 偏移 | 字节 | 字段 | 内容 |
|---:|---:|---|---|
| 0 | 4 | `magic` | `0x31464c45` |
| 4 | 4 | `format` | 2 表示带应用 ID；1 是旧格式，只能用于 `photoframe` 的 ABI 1 |
| 8 | 4 | `abi` | ELF 里有 ABI 2 清单时为 2，否则为 1 |
| 12 | 4 | `length` | ELF 字节数，52 到 1044480（1 MiB 减 4 KiB） |
| 16 | 4 | `version` | 发布版本，`--version` 指定 |
| 20 | 32 | `sha256` | ELF 的 SHA-256 |
| 52 | 32 | 应用 ID | 仅 format 2：1–31 个小写字母、数字、`_`、`-`，NUL 补齐 |
| 84 | — | 填充 | `0xff` 填到 4096 字节 |

```sh
python3 tools/module.py package --app lifecycle --version 1 \
  --input ../photoframe/examples/lifecycle/build-app/lifecycle.app.elf \
  --output /tmp/lifecycle-v1.pkg
```

- 打包工具读 ELF 里的清单（`tools/app_manifest.py`），有清单时用清单里的 ID，`--app` 与之不符会报错；有清单就生成 ABI 2 包。
- 打包工具和宿主都用 `sdk/abi2_imports.inc` 检查 ELF 的未定义符号，并检查导入与声明的次版本是否匹配。
- 发布版本只是操作者给的标签，宿主不用它判断新旧，也不阻止降级。SHA-256 只保证完整性，不是作者签名。应用是受信任的原生代码。

宿主收到包时检查：

1. 包头 `magic`、`format`、`abi` 的组合合法，`length` 与整包长度一致；
2. 应用 ID 与安装目标一致（format 1 只能装到 `photoframe`）；
3. ABI 2 包的清单 ID 与包头 ID 一致；
4. ELF 结构有效；
5. ELF 的 SHA-256 与包头一致。

## 目录

目录是 `elf_state` 分区里命名空间 `module` 下的一个 blob，键 `catalog`，每次修改整体写入并提交。

全局字段：

| 字段 | 含义 |
|---|---|
| `selected` | 已确认的前台应用 bank，开机加载它 |
| `trial_app` | 正在试运行或切换中的应用 bank，-1 表示没有 |

每个 bank：

| 字段 | 含义 | 空值 |
|---|---|---|
| `id` | 应用 ID，空串表示这个 bank 空闲 | 空串 |
| `active` | 已确认版本所在的槽 | -1 |
| `previous` | 可回退版本所在的槽 | -1 |
| `pending` | 已上传、等待激活的版本所在的槽 | -1 |
| `trial` | 1 表示正在试运行 `pending` | 0 |

宿主加载目录时做一致性检查（例如 `selected` 的应用必须有 `active`，ID 不能重复）。目录损坏时宿主不猜测、不擦除，也不加载任何应用。

目录不存在时，宿主从旧的单应用状态（同一命名空间下的键 `state`）迁移：bank 0 命名为 `photoframe`，沿用原来的 A/B 状态，其他 bank 置空。旧键保留不删。目录已存在但损坏时，不会退回旧键。

## 管理命令

所有命令都由 App Manager 串行执行。有应用在试运行（`trial_app` 不是 -1）时，除了针对它本身的回滚，其他上传、切换、删除都会被拒绝。

### 上传（op 4–6）

`app_catalog_stage()` 的步骤：

1. ID 不在目录里时，分配第一个空闲 bank；没有空闲 bank 返回内存不足（蓝牙错误 7）。
2. 该应用已有 `pending` 时拒绝。
3. 目标槽是“不是 `active` 的那个”；还没有 `active` 时用 A 槽。
4. 把 `previous` 置为 -1（目标槽里的旧版本即将被覆盖），连同 bank 分配一起写入目录。
5. 擦除目标槽，写入 ELF，读回并重新校验。
6. 最后写包头和应用 ID。
7. 把 `pending` 指向目标槽，写入目录。

任何一步中断，`active` 都不受影响；包头最后才写，写了一半的槽不会被当成有效版本。上传会消耗掉回退机会：从第 4 步起这个应用只剩 `active` 一个可用版本，直到新版本激活成功。

### ACTIVATE（op 9）

1. 要求该应用有 `pending`。目录里写入 `trial_app` = 这个 bank、`trial` = 1。
2. 卸载当前应用，加载 `pending` 槽，发 START。
3. START 成功（含声明需要的首屏）后确认：`active` ← `pending`，`previous` ← 原 `active`，`pending` 置空，`trial` 清零，`selected` ← 这个 bank，`trial_app` 清空。
4. START 失败：丢弃 `pending`，`trial_app` 清空，重新加载原来的前台应用。

### SWITCH（op 10）

切到另一个应用已确认的 `active` 版本，不碰它的 `pending`。目录先写 `trial_app` = 目标 bank，目标 START 成功后 `selected` 改为目标；失败时清空 `trial_app`，重新加载原来的应用。目标已经是前台并且正常运行时，直接返回成功。

### ROLLBACK（op 11）

| 该应用的状态 | 结果 |
|---|---|
| 正在试运行 | 取消试运行 |
| 有 `pending` | 丢弃 `pending` |
| 有 `previous` | `active` 与 `previous` 互换 |
| 都没有 | 返回状态错误（蓝牙错误 8） |

回滚的是前台应用时，宿主重新加载它。可以回滚不在前台的应用，前台不受影响。

### REMOVE（op 12）

只能删除不在前台、并且没有试运行的应用。宿主清空这个 bank 的目录项并丢弃它的缓存。槽里的字节不会立即擦除，下次分配这个 bank 时再擦，也不会被加载。这不是安全擦除。

### 没有的命令

宿主没有“激活指定槽”的命令，也没有单独擦除某个槽的命令。`previous` 为空后，想用另一个槽里的版本只能重新上传。

## 启动流程

`photoframe_plugin_init()` 在开机时依次：

1. 加载目录。上次关机时有试运行的，先取消试运行（丢弃 `pending`，清空 `trial_app`）。
2. 读启动记录，决定加载哪个槽（见下一节）。
3. 加载 `selected` 应用的 `active` 槽，发 START。
4. 加载失败时，在同一次开机里尝试回退一次。
5. START 成功后清除启动记录。

宿主在 RTC 内存里记录启动过程走到哪一步（`slots`、`services`、`boot_guard`、`slot_read`、`elf_init`、`elf_relocate`、`app_start`、`confirm`、`ready`、`select`），下次开机可以看到上一次停在哪里。

## 启动记录

启动记录在 `elf_state` 分区命名空间 `runtime`，键 `runtime_boot`，内容是 magic `0x32425452`、阶段、bank、`active`、`previous`、应用 ID 和校验和。

| 阶段 | 含义 |
|---:|---|
| 0 | 上次启动已完成 |
| 1 | 正在启动 `active` |
| 2 | 准备回退：即将把 `previous` 改成 `active` |
| 3 | 正在启动回退后的版本 |

开机时：

| 读到的记录 | 复位原因 | 宿主的做法 |
|---|---|---|
| 不存在或阶段 0 | 任意 | 写阶段 1，加载 `active` |
| 阶段 1 或 2 | 不是 USB | 写阶段 2，把 `previous` 改成 `active`、清空 `previous`，写阶段 3，加载回退版本 |
| 阶段 1、2 或 3 | USB（`ESP_RST_USB`） | 视为维护复位，不算失败：重新写阶段 1，加载 `active` |
| 阶段 3 | 不是 USB | 不加载任何应用，停在故障空闲 |
| 阶段 1 或 2，但没有 `previous` | 不是 USB | 不加载任何应用，停在故障空闲 |
| 校验和错误、格式不对、ID 与目录不符 | 任意 | 不加载任何应用 |

阶段 2 的意义是：回退过程中如果再次复位，下次开机能确认目录是否已经改好，再继续进入阶段 3。每个应用最多自动回退一次，避免坏版本反复重启刷屏。

宿主判断失败的依据只有“记录写下之后、清除之前发生了复位”，不区分断电、按电源键还是崩溃。这是已知限制，见 [启动保护](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/03-how-the-device-works.md#启动保护)。

从故障空闲恢复：对同一个应用发一次 SWITCH，宿主重新加载它的 `active`，成功后清除启动记录；或者做一次 USB 维护复位。

### 启动诊断

状态里的 `runtime_diagnostic` 形如：

```
reset=1 previous=ready current=ready guard=complete_journal
```

| 部分 | 含义 |
|---|---|
| `reset` | 本次开机的 ESP-IDF 复位原因编号 |
| `previous` | 上一次开机最后到达的启动步骤（见 [启动流程](#启动流程)），`unknown` 表示没有记录 |
| `current` | 本次开机当前所在的启动步骤 |
| `guard` | 启动保护的结论，见下表 |

| `guard` | 含义 |
|---|---|
| `complete_journal` | 启动正常完成，记录已清除 |
| `active_attempt` | 正在启动 `active`，尚未完成 |
| `usb_maintenance_attempt` | 因 USB 复位重新启动 `active` |
| `fallback_attempt` | 发现上次启动没完成，已回退到 `previous` |
| `fallback_exhausted` | 回退过的版本也没能启动，停在故障空闲 |
| `interrupted_boot_identity` | 记录的应用身份与目录不符 |
| `invalid_journal` | 启动记录损坏 |
| `catalog_not_ready` | 目录无法使用，或有未结束的试运行 |
| `open_journal` | 打不开启动记录 |

## 功能键

功能键是 GPIO4，低电平有效，内部上拉。宿主每 10 毫秒采样一次。

| 规则 | 值 |
|---|---|
| 消抖 | 30 毫秒 |
| 短按 | 稳定按下 50–600 毫秒（不含 600），松开后触发一次 |
| 长按 | 按住至少 8 秒，松开后触发 |
| 开机时已按住 | 松开前不触发任何动作 |

短按的作用取决于配对状态：

| 配对状态 | 短按 |
|---|---|
| 已有主人 | 切到下一个应用 |
| 其他状态 | 只在 `awaiting_button` 时确认配对，见 [配对 v2](ble-pairing-v2.md#屏幕与按键) |

切换应用时，宿主从当前 bank 往后循环查找下一个有 `active` 版本、并且是 ABI 2 的应用，然后按 SWITCH 的流程切换。找不到时没有反应。ABI 1 应用只能用蓝牙命令选择。

宿主正忙（正在刷屏、上传、切换）时，短按被忽略，不会事后补做，计入状态里的 `ignored_buttons`。长按不受忙状态影响，触发配对重置（见 [配对 v2](ble-pairing-v2.md#长按重置)）。BOOT 键（GPIO0）和电源键（GPIO5）不参与这些操作。

## 测试

| 测试 | 覆盖 |
|---|---|
| `tools/test_module_format.py` | 包头、ELF 和清单校验；设置 `PHOTOFRAME_TEST_ELF` 后额外检查一个真实应用 ELF |
| `tools/test_module_slots.py` | 目录、上传、确认、回退、删除、启动记录各阶段的中断恢复（用 `tests/slots/` 的 Flash/NVS 模拟） |
| `tools/test_app_manifest.py` | 清单解析和导入检查 |
| `tools/test_app_services.py` | 运行层：按键策略、App Manager 调度、宿主服务（`tests/runtime/`） |

运行方法见 [README](../README.md#测试)。
