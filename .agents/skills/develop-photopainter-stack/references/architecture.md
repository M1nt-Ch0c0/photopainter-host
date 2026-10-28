# PhotoPainter implementation architecture

Canonical current contracts: [ABI 2 runtime](../../../../docs-runtime-v2.md), [five-bank storage and Wi-Fi](../../../../docs-multi-apps.md), and [reader-facing diagrams](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/ARCHITECTURE.md).

Pairing v2 source is implemented but **尚未实机验证 / not hardware-validated**.
Use [the pairing contract](../../../../docs/ble-pairing-v2.md) for current authentication;
old fixed-token firmware results are historical evidence only.

- `ai-quota-frame` owns quota access, layout, strict six-color PNG, outbound pushing and the serialized device backend. The independent `photopainter-console` repository owns the native GUI and calls that backend over its protected local socket. Automatic pushing does not select device apps and must not listen on the retired :8787 port.
- `photopainter-host` owns physical pairing, NVS owner/tombstone, per-connection authentication, SD/NVS Wi-Fi, catalog, the only manager worker, ELF ABI dispatch, bounded services and ABI 2 board I/O. HTTP control is retired with 410; health is read-only.
- `photoframe` owns PNG validation, logical graphics, initial pages and application restoration. Its default app ELF uses ABI 2; `PHOTOFRAME_ABI=1` and `.so` retain legacy board code.
- `esp32s3` owns navigation and explanation; it participates in no build.

The host never builds or embeds a sibling business ELF. Apps use the fixed IDF commit and Registry elf_loader 1.3.3, unchanged. ABI 2 imports only SDK services and allowlisted libc/compiler functions; ABI 1 retains original exports. Native ELF is trusted shared-address-space code, not a sandbox.

A display app's ABI 2 START is ready only after a correct result and a host-observed complete refresh. INPUT(PNG) is specific to compatible apps. STOP returns before unload. All sources use the same admission/manager boundary; client disconnect does not release live input. Stage/activate/switch remain explicit. GPIO 4 selects only confirmed ABI 2 active versions.

During a pairing request GPIO4 short press confirms only after the complete
comparison screen has been displayed and the client has durably stored its
candidate. Holding at least 8 seconds and releasing always requests reset,
including during a display. New work is rejected immediately, current physical
display and borrowed inputs finish, then worker drain/exact ACK permits the
manager to persist the unbound tombstone. Reset does not erase Wi-Fi or APPs.

PNG generation and decoding preserve opaque exact black/white/yellow/red/blue/green, 800×480, noninterlaced, no trailing bytes. Host board service alone rotates/packs logical ABI 2 pixels and preserves E6 final POWER_OFF completion. A matching signed BLE image result proves physical completion; upload/stage success and HTTP health 200 do not. BLE images borrow PSRAM through the same manager render call and retain bounded per-boot receipts for safe reconnect queries. Pending client transactions also pin the pairing BID.

Wi-Fi JSON authority and atomic .tmp/.bak migration/save are unchanged. No app receives pairing keys, Wi-Fi credentials or host usage keys. Runtime cache is optional RAM, not SD persistence. No device WebUI, whole-firmware OTA, background app tasks or new public service is added. The CPM browser plugin uses CPA's existing resources and a shared Go/WASM engine; its negotiated browser relationship and the native client's relationship are independent owners, transferred through physical reset.

The framework/application boundary and current API guide are maintained in the [developer Wiki](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Home.md). The current image route is app-owned: photoframe v20 uses ABI 2.2 single-owner CoC, while its ABI 2.1 GATT route remains compatible. A real Mac CoC transfer and physical completion receipt were recorded on 2026-10-27. The older host-specific image path remains compatibility code. Keep framework entry documentation about generic services and app management; business input/sending commands belong to app/client guides.
