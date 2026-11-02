# photopainter_board

宿主的屏幕和电源驱动组件。ABI 2 应用调用 `app_host_display_v2()` 交来一帧逻辑画面，宿主经这个组件把它送上 Spectra 6 电子纸。组件不解码 PNG，也不知道是哪个应用在显示。显示链路的整体原理见 [设备是怎么工作的](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/03-how-the-device-works.md#显示链路)。

## 接口

```c
int photopainter_board_display(const uint8_t *pixels, size_t bytes);
int photopainter_board_pack(const uint8_t *pixels, size_t bytes, uint8_t *wire, size_t wire_bytes);
```

| 函数 | 作用 |
|---|---|
| `photopainter_board_pack` | 先检查整帧（384000 字节，每个值 0–5），再旋转 180° 并把每两个像素打包成 1 字节，生成 192000 字节的面板数据 |
| `photopainter_board_display` | 打包后打开屏幕电源、初始化面板、写入数据、触发刷新、等待完成并断电，全部完成才返回 |

逻辑像素值到面板色码的映射：

| 逻辑值 | 颜色 | 面板色码 |
|---:|---|---:|
| 0 | 黑 | 0 |
| 1 | 白 | 1 |
| 2 | 黄 | 2 |
| 3 | 红 | 3 |
| 4 | 蓝 | 5 |
| 5 | 绿 | 6 |

## 硬件

| 信号 | GPIO |
|---|---|
| 屏幕 SPI：SCLK、MOSI | 10、11 |
| 屏幕 DC、CS、RST、BUSY | 8、9、12、13 |
| AXP2101 电源芯片 I²C：SDA、SCL | 47、48 |

- SPI 时钟 40 MHz。
- 屏幕供电来自 AXP2101 的 ALDO4（使能寄存器 `0x90` 第 3 位，电压寄存器 `0x95`，3.3 V）。ALDO3 接的是音频电源，驱动不碰它。寄存器用读改写，保留其他位。
- 每个 BUSY 等待最长 120 秒，超时返回 `PHOTOFRAME_ERR_BUSY_TIMEOUT`。

调用由 App Manager 串行，组件在两次调用之间不占用总线。旧的 ABI 1 应用自带驱动，也由 App Manager 串行。

## 来源与许可

`src/epd_e6.c` 的命令和数据序列改编自微雪 PhotoPainter 示例代码的 `display_bsp.cpp`，源文件保留原 MIT 声明，许可见 `LICENSE.Waveshare.txt`。改动电源、引脚或时序需要更新宿主固件；只改 PNG 解码只需更新应用。
