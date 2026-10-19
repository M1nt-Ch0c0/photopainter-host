---
name: develop-photopainter-stack
description: Bootstrap, build, deploy, continue development, and diagnose the three-repository ESP32-S3 PhotoPainter stack made of photopainter-host, photoframe, and ai-quota-frame. Use when Codex starts in a new computer or checkout, installs ESP-IDF, Go, Chrome, or serial tools, coordinates changes across the repositories, builds or flashes firmware, provisions Wi-Fi and push authentication, sends a frame, or debugs USB, Wi-Fi, HTTP, ELF loading, PNG validation, and Spectra 6 refreshes.
---

# Develop PhotoPainter Stack

Treat `photopainter-host`, `photoframe`, and `ai-quota-frame` as one deployed system with separate repositories. Preserve their boundaries and validate the complete path from source to physical display.

## Start every task

1. Locate the three sibling checkouts. Do not assume a port, device IP, operating system, or workspace path.
2. Read the complete `AGENTS.md` in every repository that the task may change.
3. Read the relevant reference before acting:
   - New computer, toolchain, or checkout: [bootstrap.md](references/bootstrap.md)
   - Cross-repository behavior, ELF ABI, or ownership: [architecture.md](references/architecture.md)
   - USB, Wi-Fi, HTTP, PNG, or display failures: [debugging.md](references/debugging.md)
4. From `photopainter-host`, run `python3 tools/doctor.py` before changing a deployment. Pass `--device-url` when the device URL is known. Use `--probe-serial` only when a device reset is acceptable.
5. Inspect Git status in every affected repository. Preserve unrelated user changes.

## Choose the workflow

- For host firmware work, build the independent framework and verify its fixed ABI exports. Read [module slots](../../../docs-module-slots.md) for deployment.
- For display-component work, run native tests, build both `.app.elf` and `.so`, check imports against the declared host ABI (ABI 2 allowlist or legacy ABI 1), then package and stage the app ELF in the inactive slot. Rebuild the host only for a framework or ABI change.
- For PC service work, run `make check`, verify the rendered PNG contract, and test retry behavior with an HTTP test server before using hardware.
- For a real-device push, stop any automatic pusher that could race the test, validate the PNG first, issue one request, and wait for the final HTTP result. Restart the service only after the test is understood.
- For deployment on a new computer, follow the bootstrap reference in order and keep all secrets outside Git.

For cooperative ABI 2 work, read [runtime guide](../../../docs-runtime-v2.md). The host owns ABI 2 board display I/O; apps own content and restore policy. Keep ABI 1 compatibility.

## Enforce safety gates

- Never print, commit, or copy secret values into commands, logs, documentation, issues, or chat. Let trusted programs consume `.env` and `secrets.env` without displaying them.
- Never use `aitjcize/esp32-photoframe` or a Waveshare complete firmware as the base. Only reuse the documented display pins and E6 timing.
- Keep `elf_loader` as the unforked Registry dependency `^1.3.3`.
- Do not add a WebUI, album, whole-firmware OTA, Home Assistant, deep sleep, public port mapping, or a second host HTTP service.
- Do not flash or provision a device unless the user authorized it. Back up and hash the 16 MiB Flash first; back up an installed microSD unless the user explicitly waives that backup for the run.
- Treat opening the native USB serial port as a possible reset. Do not use it merely to check liveness while a push is active.
- Treat HTTP `200` as the only protocol proof that the physical refresh and final POWER_OFF wait completed. A retained e-paper image is not proof that the ESP32 is powered or online.

## Finish with evidence

- Report the exact repository commits tested, commands run, and whether hardware was involved.
- Separate transport success from display success. Record the HTTP status and require visual confirmation for orientation, colors, and content after a real refresh.
- If a test cannot reach `200`, stop automatic retries before they can repeatedly refresh the panel and preserve the first causal device log.
