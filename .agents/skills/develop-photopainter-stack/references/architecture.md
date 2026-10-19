# PhotoPainter implementation architecture

Canonical current contracts: [ABI 2 runtime](../../../../docs-runtime-v2.md), [five-bank storage and Wi-Fi](../../../../docs-multi-apps.md), and [reader-facing diagrams](https://github.com/M1nt-Ch0c0/esp32s3/blob/codex/multi-wifi-apps/ARCHITECTURE.md).

- `ai-quota-frame` owns quota access, layout, strict six-color PNG and outbound pushing. It does not select device apps and must not listen on the retired :8787 port.
- `photopainter-host` owns HTTP/authentication, SD/NVS Wi-Fi, catalog, the only manager worker, ELF ABI dispatch, bounded services and ABI 2 board I/O.
- `photoframe` owns PNG validation, logical graphics, initial pages and application restoration. Its default app ELF uses ABI 2; `PHOTOFRAME_ABI=1` and `.so` retain legacy board code.
- `esp32s3` owns navigation and explanation; it participates in no build.

The host never builds or embeds a sibling business ELF. Apps use the fixed IDF commit and Registry elf_loader 1.3.3, unchanged. ABI 2 imports only SDK services and allowlisted libc/compiler functions; ABI 1 retains original exports. Native ELF is trusted shared-address-space code, not a sandbox.

A display app's ABI 2 START is ready only after a correct result and a host-observed complete refresh. INPUT(PNG) is specific to compatible apps. STOP returns before unload. All sources use the same admission/manager boundary; client disconnect does not release live input. Stage/activate/switch remain explicit. GPIO 4 selects only confirmed ABI 2 active versions.

PNG generation and decoding preserve opaque exact black/white/yellow/red/blue/green, 800×480, noninterlaced, no trailing bytes. Host board service alone rotates/packs logical ABI 2 pixels and preserves E6 final POWER_OFF completion. Push 200 proves that physical completion; management 200 has a different contract.

Wi-Fi JSON authority, atomic .tmp/.bak migration/save and all credential boundaries are unchanged. No app receives Wi-Fi credentials or host usage keys. Runtime cache is optional RAM, not SD persistence. No WebUI, OTA, background app tasks or new public service is added.
