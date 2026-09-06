# 多 Wi-Fi / 多应用本地验证

本记录针对 `codex/multi-wifi-apps`；新增固件尚未部署，不能引用此前单应用的真实刷屏记录作为本次验证。

- 固定 ESP-IDF：`5e6f53cdb31fe5708eae3f55af9737be2822db22`，Registry elf_loader 1.3.3 保持不变。
- `idf.py build` 通过；宿主独立构建，无 sibling ELF 编译依赖。
- `PHOTOFRAME_TEST_ELF=../photoframe/build-app/photoframe.app.elf python3 -m unittest discover -s tools -p 'test_*.py'`：50 项全部通过、无跳过。
- 三张总览 Mermaid 图解析通过；存储布局由 ESP-IDF 分区生成及尺寸检查验证。
- 只读设备检查确认原固件在线（无令牌请求 401），未打开串口、未中断当前定时推图。

## 覆盖范围

C 应用目录/真实加载调用层：旧状态迁移、旧 trial 恢复、损坏新目录拒绝、独立安装/更新、五应用容量与移除重用、跨应用包 ID 拒绝、写入/日志失败、单全局 trial、确认失败重启恢复、候选 payload 损坏、ELF 重定位失败、回调缺失、PNG 拒绝不确认、驱动失败恢复及至多一个加载实例。Flash/NVS 与 ELF 执行在本地用确定性 mock；不是板上断电证明。

实际 C Wi-Fi 连接任务：每次连接等待 20 秒上界，优先级失败后第二网络成功即停止，全失败 5 秒后再遍历，工作网络断线后重新从第一组尝试；配置字节不变，radio stop 后再启动下一组，RAM 配置和关闭省电。

C SD JSON 解析与 Python NVS 编码：顺序、空列表、重复项/键、版本、超长与深度限制、NUL、SSID/密码边界、主文件缺失时 `.bak`、损坏主文件保持原样。CLI：format-2 ID 位置、旧格式兼容、各管理操作目标、推图应用 ID 头。

## 尚待实机验证

1. 插入 SD：原 JSON 格式按优先级连接两个可开关的真实 2.4 GHz 网络，断线重连、全部不可用后恢复、重启保留列表；无卡旧 NVS 兼容。
2. 备份后部署新分区表/宿主，验证原 photoframe 的 A/B 和 NVS 导入。
3. 至少两个独立应用分别安装、更新、切换、回退；确认未运行应用的 A/B 不变。
4. 真实掉电中断候选、包写入及切换恢复；无 ID 的原定时 pusher 不误投其他应用。
5. 每次用于确认应用的推图需 HTTP 200 和人工检查内容、方向与颜色。

构建 SHA-256：

- 固件：`ae86b590bcabc5c92c9cd8962a1179915862944946850efc985f0c8d9223d470`
- 宿主 ELF：`502383bd166e36ee3c027ecb126b4258bbaa25ab12cc41e96252ba98ebc1a814`
