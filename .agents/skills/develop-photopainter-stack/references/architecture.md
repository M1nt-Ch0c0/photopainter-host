# PhotoPainter architecture for implementers

Reader-facing explanations are in the esp32s3 docs: [how the device works](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/03-how-the-device-works.md), [app API reference](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/05-app-api-reference.md) and [Bluetooth](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/06-bluetooth.md). Byte-level host contracts are in this repository's `docs/`. This page lists only the ownership boundaries and invariants to keep in mind when changing code.

## Ownership

| Repository | Owns |
|---|---|
| `photopainter-host` | BLE peripheral, pairing v2 and the NVS owner record, per-connection authentication, signed management protocol, five-bank catalog and A/B slots, boot protection, ELF loading, the single App Manager, host services (ABI 2.0–2.3), board display/power I/O, SD/NVS Wi-Fi, app SDK originals in `sdk/` |
| `photoframe` | The default app: first screen and cache restore, strict PNG decoding, the image transaction protocol and its three adapters (ABI 2.1 GATT, 2.2 CoC, 2.3 channel), example apps, the SDK copy, the legacy ABI 1 app with its own driver |
| `ai-quota-frame` | Quota collection, rendering of the six-color PNG, BLE client and CLI, background daemon, USB maintenance tool, CPM browser plugin |
| `photopainter-console` | macOS GUI that talks to the `ai-quota-frame` daemon over its local socket |
| `esp32s3` | Documentation only |

The host never builds or embeds an app. Apps are trusted native ELF code in the host address space, not a sandbox.

## Runtime invariants

- Exactly one App Manager task runs every app entry, catalog change and display. BLE callbacks only copy data. Every source of work (BLE, button, timer, network) shares one admission lock.
- Each event is reported exactly once with `app_host_complete_v2()`. START succeeds only with `APP_READY` and, for apps that declare `APP_REQUIRES_DISPLAY`, a completed display in that same START.
- Event logic has a 5-second watchdog; a display call has 650 seconds. A timeout restarts the device.
- Install, activate, switch, rollback and remove are explicit commands. Upload stages into the inactive slot and clears `previous`. There is no command to activate a chosen slot.
- Boot protection allows one automatic fallback per app and treats any reset inside the boot window as a failure, except a USB reset.
- The function key (GPIO4) short press switches to the next confirmed ABI 2 app when bound, and approves pairing during `awaiting_button`. Holding at least 8 seconds always requests a pairing reset, even during a refresh; the reset commits after the current refresh and borrowed inputs finish.
- Every queued BLE job carries the authorization epoch and is rechecked before side effects.
- Only the board component rotates and packs logical pixels. A display returns success only after the refresh and final power-off complete.

## Image contract

- PNG: 800×480, non-interlaced, fully opaque, only exact black/white/yellow/red/blue/green, no trailing data, at most 5 MiB. Validation completes before any panel I/O.
- photoframe keeps the last four completion receipts in RAM; they are lost on reboot or app switch.
- A client persists the transaction identity (device ID, BID, request ID, SHA-256, length, app generation) before COMMIT, and after a lost reply only queries that request.

## Wi-Fi

SD `config/wifi.json` is authoritative when present, NVS is the fallback, and the first boot with a card migrates NVS or `wifi.txt` to SD. Writes use `.tmp` then `.bak` publication. Apps never receive credentials. See `docs/wifi-config.md`.
