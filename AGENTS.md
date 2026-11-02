# photopainter-host agent guide

Read this before changing the host firmware. Project-wide documentation (architecture, app API, Bluetooth design, maintenance) lives in the [esp32s3 docs](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/README.md). For a new computer, cross-repository work or hardware diagnosis, also read the project skill at [.agents/skills/develop-photopainter-stack/SKILL.md](.agents/skills/develop-photopainter-stack/SKILL.md).

## Repository role

This repository owns the resident host firmware:

- BLE peripheral, physical pairing v2, per-connection authentication and the signed management protocol (`docs/ble-protocol.md`, `docs/ble-pairing-v2.md`);
- the five-bank application catalog, A/B slots, package validation, trial/confirm/rollback and boot protection (`docs/app-catalog.md`);
- ELF loading and the single App Manager that serializes every app call, catalog change and display;
- host services for apps (ABI 2.0–2.3): display, cache, timer, time, app GATT service, CoC, generic message channel;
- the board display/power component `components/photopainter_board`;
- SD/NVS Wi-Fi configuration (`docs/wifi-config.md`);
- the app SDK originals in `sdk/`.

Other repositories own the rest: `photoframe` (PNG decoding, image transactions, example apps, SDK copy), `ai-quota-frame` (PC service, CLI, USB maintenance tool, browser plugin), `photopainter-console` (macOS GUI), `esp32s3` (documentation only).

## Constraints

- Target only the 7.3-inch 800×480 Spectra 6 ESP32-S3 PhotoPainter, N16R8.
- Require ESP-IDF commit `5e6f53cdb31fe5708eae3f55af9737be2822db22`. `CMakeLists.txt` and `tools/doctor.py` enforce it.
- Use the Component Registry `espressif/elf_loader` 1.3.3 unmodified. Never fork, vendor or patch it. Never edit `managed_components/` or build output.
- Do not base work on `aitjcize/esp32-photoframe` or a Waveshare complete firmware. Only the documented display pins and E6 timing are reused.
- Keep `partitions.csv` unchanged. Host updates write only the factory partition at `0x10000`.
- Out of scope: device WebUI, photo album, whole-firmware OTA, Home Assistant, deep sleep, public port mapping, a second HTTP service, app access to SD.
- HTTP control routes return 410; only `GET /health` is served. Do not restore token-based control. All control goes through paired BLE and the same App Manager and validation paths.
- Validate every request completely before any display I/O.
- Keep `sdk/photopainter_app.h` and `sdk/abi2_imports.inc` byte-identical with `../photoframe/components/app_sdk/`. Published struct layouts are frozen; new capability means a new ABI minor version, new functions and new events.
- Keep ABI 1 and older ABI 2 minor versions loadable.
- When you change a wire format, command, limit or status field, update the matching page in `docs/` in the same change.

## Build and test

```sh
. /path/to/esp-idf/export.sh
git -C "$IDF_PATH" rev-parse HEAD          # must print the pinned commit
idf.py -B build-host -DSDKCONFIG=build-host/sdkconfig build
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/doctor.py --strict
```

- The host builds independently of sibling repositories.
- Tests compile production C with the local `cc`, OpenSSL and ASan/UBSan. Set `PHOTOFRAME_TEST_ELF` to include a real app ELF and `IDF_PATH` to include the NVS generator check.
- Use a fresh build directory when `sdkconfig.defaults` changes; an existing `sdkconfig` does not pick up new defaults.
- Use `idf.py fullclean` only when a clean rebuild is required, never to discard unrelated changes.

## Hardware safety

- Flash or provision a device only with explicit user authorization and an exactly identified port.
- Before any Flash write, back up all 16 MiB and verify the hash. Back up an installed microSD unless the user waives it for that run. Prefer `ai-quota-frame/deploy/usb-maintenance.py`, which does both and writes only factory.
- Never run `idf.py flash` or a whole-chip write on a device in use; it overwrites the partition table, NVS and app slots.
- Writing a full NVS image (`tools/provision.py --port`, `usb-provision`) also erases the pairing owner.
- Opening the native USB serial port can reset the device. Do not use it as a liveness check.
- Stop automatic pushers before one-shot display tests. After an ambiguous image commit, query the original receipt; never resend.
- Display success requires a matching signed completion receipt plus human confirmation of content, orientation and colors. Upload or staging success is not display success. A retained e-paper image does not prove the device is powered or running.

## Secrets

Never open, print, commit or paste `secrets.env`, private Wi-Fi files, NVS images, Flash or SD backups, or logs containing credentials. Tools may consume these files without displaying them. Check `.gitignore` before Git operations.

## Commits

Do not commit, push or flash unless the user asks. Keep unrelated user changes intact.
