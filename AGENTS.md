# PhotoPainter host agent guide

**Source and documentation baseline:** main includes the resident BLE/ABI 2.1 framework, photoframe-owned image reception and its desktop client. See the [version/status matrix](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Status-and-Evidence.md) for source commits and remaining hardware acceptance limits.

This is the canonical entry point for AI-assisted work on the three-repository PhotoPainter system. For a new computer, deployment, cross-repository change, or hardware investigation, read the complete project skill at [.agents/skills/develop-photopainter-stack/SKILL.md](.agents/skills/develop-photopainter-stack/SKILL.md) before acting.

## Read order

1. Read this file and `README.md`.
2. Read the project Skill and only the references relevant to the task.
3. Read `../photoframe/AGENTS.md` before changing the display component.
4. Read `../ai-quota-frame/AGENTS.md` before changing the PC service.
5. Run `python3 tools/doctor.py` before deployment or hardware diagnosis.

## Repository role

This repository owns the resident framework: network configuration/stack, authentication, application OTA/catalog, ELF loading, the single manager and generic host services. ABI 2 board I/O belongs to `photopainter_board`. PNG decoding, first-screen policy and the approved app-owned BLE receive protocol belong to `photoframe`; collection/rendering/sending belongs to `ai-quota-frame`.

The developer entry is the [framework Wiki](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/wiki/Home.md). Keep framework README workflows about loading and managing applications. Existing HTTP PNG and dedicated host BLE PNG paths remain compatibility features; do not extend the dedicated host image protocol or flash the host for the new app-owned route without a demonstrated generic ABI gap and the user's applicable authorization. The app-owned receive route is implemented in sibling photoframe; its protocol and validation remain application-owned.

The host now loads an independent app ELF from A/B data slots. Read `docs-module-slots.md` for package, activation, confirmation and rollback. The explicit user-requested module update architecture supersedes the former embedded-only/no-module-update design.

## Non-negotiable constraints

- Target only the 7.3-inch 800×480 N16R8 ESP32-S3 PhotoPainter with Spectra 6.
- Require ESP-IDF commit `5e6f53cdb31fe5708eae3f55af9737be2822db22`.
- Resolve `espressif/elf_loader: ^1.3.3` from the Component Registry. Never fork, vendor, or patch it.
- Do not base work on `aitjcize/esp32-photoframe` or a Waveshare complete firmware.
- Do not add WebUI, album, whole-firmware OTA, Home Assistant, deep sleep, or an SD dependency for applications. The user-requested SD Wi-Fi boot source and first migration from NVS/wifi.txt are supported. Preserve valid JSON authority and atomic .tmp/.bak publication; never format the card. Independent per-app A/B updates are supported.
- Keep HTTP image pushes on `POST /api/push`, module management on `/api/module`, and SD Wi-Fi configuration on `/api/wifi`, all on port 80. The user-requested BLE image/app transports share the same manager and validation paths. Do not add public exposure or port mapping.
- Preserve status semantics: unconfigured token 503, missing or wrong token 401, over 5 MiB 413, invalid compatible image 4xx, and 200 only after physical refresh plus final POWER_OFF wait.
- Reject every invalid request before display I/O.

Read `docs-multi-apps.md` for the five-bank application catalog, format-2 identity, switch trials, legacy migration, and SD/NVS Wi-Fi priority.

Read `docs-runtime-v2.md` for ABI 2, cooperative events, host services, GPIO 4 switching and bounded boot recovery. Preserve ABI 1 and the existing five-bank layout.

## Build and test

The host builds independently of `photoframe`. The sibling layout is convenient for packaging module builds, but is no longer a host build dependency.

```bash
. /path/to/esp-idf/export.sh
git -C "$IDF_PATH" rev-parse HEAD
idf.py set-target esp32s3
idf.py build
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/doctor.py --strict
```

The IDF revision check must match exactly. Do not edit generated build output or `managed_components`. Use `idf.py fullclean` only when a clean rebuild is necessary and never to discard unrelated source changes.

## Hardware safety

- Flash or provision only after explicit authorization and exact port identification.
- Before flashing, back up and hash all 16 MiB of Flash. Back up an installed microSD unless the user explicitly waives it for that run.
- Treat opening native USB serial as a device reset that can abort an active HTTP request.
- Stop automatic pushers before one-shot display tests. Do not allow ambiguous transport failures to cause repeated panel refreshes.
- Require HTTP 200 or an authenticated BLE image-completion receipt, then human visual confirmation. An upload/staging acknowledgement is not physical completion. An e-paper image remains visible without power.

## Secrets

Never open, print, commit, or paste `secrets.env`, NVS images, Flash/SD backups, or token-bearing logs. Trusted provisioning and push tools may consume secrets without displaying them. Keep secret files outside generated artifacts and confirm ignore rules before Git operations.
