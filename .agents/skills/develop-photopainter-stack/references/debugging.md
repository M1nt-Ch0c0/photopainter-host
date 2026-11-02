# Debug PhotoPainter safely

Symptom-by-symptom troubleshooting for users is in the [maintenance guide](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/08-maintenance.md#常见症状). This page covers what an implementer should check and which observations prove what.

## Start with the doctor

From `photopainter-host`:

```sh
python3 tools/doctor.py
```

It reports Python, Chrome, the ESP-IDF commit, `idf.py`, sibling repository state and serial ports. It never reads `.env` or `secrets.env`. `--json` gives machine-readable output. `--probe-serial --serial PORT` captures boot output and may reset the device; use it only when interrupting the device is acceptable.

## Read the signed status first

`ai-quota-frame ble status` (or op 3 directly) is the primary diagnostic and does not touch the panel. Fields that matter most:

| Field | Use |
|---|---|
| `loaded_app`, `runtime_ready`, `phase` | Is an app running? `phase=fault` with `loaded_app=-1` means fault idle: no app, still manageable over BLE |
| `apps[]` | Per-app `active`, `previous`, `pending`, `trial` and slot `versions` |
| `trial_app` | A trial in progress blocks other installs and switches |
| `runtime_diagnostic` | `guard=` shows boot protection: `fallback_attempt` means it fell back, `fallback_exhausted` means it stopped |
| `uptime_ms`, `reset_reason` | Distinguishes a BLE disconnect from a reboot (1 power-on, 3 software, 4 panic, 5/6 watchdog, 9 brownout, 11 USB) |
| `payload_abi_minor` | Below 3 means the browser falls back to the slow GATT path |

Op 28 (`help`, `apps`, `ps`, `free`, `uptime`, `wifi`) gives the same data as short text and works during an upload. Field definitions are in `docs/ble-protocol.md`.

## What each observation proves

| Observation | Proves | Does not prove |
|---|---|---|
| E-paper shows an image | The panel retains its last frame | The device is powered or running |
| USB serial device exists | The ESP32-S3 enumerated | Wi-Fi, BLE or a running app |
| BLE scan finds `PP-XXXX` | The host is running and advertising | Pairing is valid or an app is running |
| BondAuth succeeds | This client is the current owner | An app is ready |
| `GET /health` returns 200 | Wi-Fi and the HTTP listener work | Pairing, a running app or a refresh |
| `/api/push` returns 410 | Expected; HTTP control is retired | Anything about BLE |
| Upload or package commit succeeds | Data arrived and was staged | Activation or display |
| Signed image receipt `complete`, error 0, matching ID/length/SHA-256 | Refresh and final power-off finished | Correct content, orientation and colors |

Always ask for visual confirmation after a real refresh.

## Lost or uncertain image results

If a COMMIT reply is lost, reconnect, BondAuth, check that device ID, BID and app generation are unchanged, and QUERY the original request ID. Never resend with a new ID. A changed boot, changed BID, app switch or missing receipt means the outcome is unknown: stop automatic retries for that image and ask a human to check the screen. photoframe keeps four receipts in RAM only.

## Boot and ELF failures

1. Read status. Check the selected app's `active` slot, `versions` and `runtime_diagnostic`.
2. `guard=fallback_attempt` after a plain power loss is expected behaviour (boot protection does not distinguish reset causes). Re-upload and activate the desired version; doing it twice puts it in both slots.
3. `guard=fallback_exhausted` or `phase=fault`: send SWITCH to the same app (`ai-quota-frame ble switch --app photoframe`). If START still fails, install a known-good package.
4. For a package the host rejects (BLE error 9), check the ELF imports against `sdk/abi2_imports.inc`, that the declared ABI minor covers the imports and does not exceed the host's, and that the manifest ID matches `--app`.
5. Confirm the Registry `elf_loader` is 1.3.3 and `managed_components/` is unmodified.

Reproduce image errors in photoframe's native tests before touching the board component.

## Serial boot log

If a reset is acceptable, capture one complete boot. Expected lines include the project name `photopainter_host`, the app version from the host Git revision, ESP-IDF `v6.0.3`, `ELF loader version: 1.3.3`, and `app_runtime: ready bank=... slot=... ABI=... version=...`. With Wi-Fi configured, `wifi: trying profile i/n` and `wifi: IPv4: ...` follow. Do not repeatedly open and close the port; make sure no other monitor owns it.

## Wi-Fi

Wi-Fi is optional; BLE management and images do not need it. Discovery state 3 means the configuration is invalid. A damaged SD `config/wifi.json` is never replaced by `.bak` or NVS. The station tries each network for up to 20 seconds and retries the list every 5 seconds. Rules are in `docs/wifi-config.md`.

## Display failures

| Symptom | Likely layer |
|---|---|
| Immediate error, no panel activity | AXP2101, I²C, SPI or init failure (`APP_ERR_DISPLAY`) |
| Long wait then `APP_ERR_TIMEOUT` | BUSY stayed low for 120 seconds |
| Reply lost after visible refresh | Completion is not proven until the original receipt is recovered |

Keep SCLK 10, MOSI 11, DC 8, CS 9, RST 12 and BUSY 13 unchanged. The panel rail is AXP2101 ALDO4; ALDO3 is audio. Do not experiment with power rails or display timing without explicit hardware scope and a recovery plan.

## Unexpected resets

`reset_reason=1` with a smaller `uptime_ms` points to power. Computer USB ports can be unstable under BLE and refresh load; use an independent USB power adapter. Firmware has no battery or PMIC shutdown telemetry, so power problems can only be inferred from the reset reason.
