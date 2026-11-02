---
name: develop-photopainter-stack
description: Bootstrap, build, deploy, continue development, and diagnose the ESP32-S3 PhotoPainter stack spanning photopainter-host, photoframe, ai-quota-frame, photopainter-console and the esp32s3 docs. Use when starting in a new computer or checkout, installing ESP-IDF, Go, Chrome or serial tools, coordinating changes across the repositories, building or flashing firmware, pairing a client, installing apps, sending a frame, or debugging BLE, USB, Wi-Fi, ELF loading, PNG validation and Spectra 6 refreshes.
---

# Develop PhotoPainter Stack

Treat `photopainter-host`, `photoframe`, `ai-quota-frame` and `photopainter-console` as one deployed system with separate repositories. `esp32s3` holds the shared documentation and takes part in no build. Preserve repository boundaries and validate the complete path from source to physical display.

System facts that hold for every task:

- All device control goes over BLE after physical pairing v2. The host rejects the old fixed-token handshake (op 1) and returns 410 for every HTTP control route; `GET /health` is the only HTTP endpoint.
- The host owns discovery, pairing, the owner record, authentication, the app catalog and display I/O. Apps own their content and protocols; photoframe owns image reception and completion receipts.
- The native client and each browser are independent owners. Changing owner requires the physical 8-second reset on the frame.

## Start every task

1. Locate the sibling checkouts. Do not assume a port, device ID, IP, operating system or workspace path.
2. Read `AGENTS.md` in every repository the task may change.
3. Read the relevant reference:
   - New computer, toolchain or checkout: [bootstrap.md](references/bootstrap.md)
   - Ownership across repositories and runtime contracts: [architecture.md](references/architecture.md)
   - BLE, USB, Wi-Fi, ELF, PNG or display failures: [debugging.md](references/debugging.md)
4. From `photopainter-host`, run `python3 tools/doctor.py` before deployment work. Add `--probe-serial --serial PORT` only when a device reset is acceptable.
5. Check `git status` in every affected repository and preserve unrelated changes.

## Choose the workflow

- **Host firmware**: build with the pinned IDF, run the host Python suite, and update `docs/` for any wire or catalog change. Install on a device only through the factory-only USB flow.
- **App (photoframe or examples)**: run native tests, build `.app.elf`, check imports against `sdk/abi2_imports.inc`, package with `photopainter-host/tools/module.py package`, then install and activate over BLE. Rebuild the host only when the ABI changes, and update the host before installing an app that needs a newer ABI minor.
- **PC service**: run `make check`, verify the rendered PNG contract, and test retry behaviour against test servers before using hardware.
- **Real-device image**: stop any automatic pusher, validate the PNG, use the paired owner, send one transaction and wait for the signed completion receipt. Resume automatic work only afterwards.
- **New computer**: follow the bootstrap reference in order and keep all secrets outside Git.

## Safety gates

- Never print, commit or copy secret values into commands, logs, documentation, issues or chat. Let trusted programs read `.env` and `secrets.env` without displaying them.
- Do not flash or provision a device without user authorization. Back up and hash the full 16 MiB Flash first, and back up an installed microSD unless the user waives it for that run.
- Host updates write only factory at `0x10000`; never run `idf.py flash` on a device in use. Writing a full NVS image erases the pairing owner.
- Opening the native USB serial port can reset the device; do not use it as a liveness check.
- Keep `elf_loader` the unmodified Registry 1.3.3. Do not use `aitjcize/esp32-photoframe` or a Waveshare complete firmware as a base.
- Do not add a device WebUI, album, whole-firmware OTA, Home Assistant, deep sleep, public port mapping or a second host HTTP service.

## Finish with evidence

- Report which repository commits were tested, which commands ran, and whether hardware was involved.
- Separate upload success from display success. Display success needs a signed completion receipt whose request ID, length and SHA-256 match, plus visual confirmation of content, orientation and colors.
- If completion cannot be confirmed, stop. Query the original request; a changed boot, changed pairing BID or missing receipt means the outcome is unknown and must not trigger another automatic draw.
