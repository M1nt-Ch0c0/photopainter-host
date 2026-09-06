# PhotoPainter stack architecture

For the reader-facing Chinese overview, Mermaid diagrams, coupling boundaries and change-impact matrix, see the [stack architecture](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/ARCHITECTURE.md). This reference records the implementation contracts for development.

## Repository ownership

| Repository | Owns | Does not own |
|---|---|---|
| `photopainter-host` | ESP32 boot, NVS configuration, Wi-Fi station, Bearer authentication, HTTP request buffering, ELF relocation and invocation | PNG decoding policy, E6 implementation, quota collection |
| `photoframe` | Strict PNG decoding, six-color validation, 180-degree packing, AXP2101 power setup, Spectra 6 E6 refresh, `.app.elf` and `.so` artifacts | Wi-Fi, HTTP, secrets, scheduling |
| `ai-quota-frame` | Quota collection, 800×480 six-color rendering, active HTTP push, latest-frame queue and retries | Device firmware, inbound HTTP service, device provisioning |
| `esp32s3` | Links to the three repositories | Source code and build artifacts |

Keep the repositories as siblings for convenient development; the host builds independently:

```text
workspace/
├── photopainter-host/
├── photoframe/
└── ai-quota-frame/
```

## Runtime data flow

```text
CLIProxyAPI / optional usage source
              │
              ▼
       ai-quota-frame
       render strict PNG
              │ POST raw image/png + Bearer token
              ▼
       photopainter-host
   authenticate → bound size → buffer
              │ host bridge symbols
              ▼
  A/B slot photoframe.app.elf
  relocate → decode all → validate all
              │ only after validation
              ▼
   AXP2101 → SPI → Spectra 6 E6
              │ final BUSY + POWER_OFF
              ▼
             HTTP 200
```

The device is the HTTP server on port 80. `ai-quota-frame` is only an outbound client and must not listen on the retired `:8787` port.

## ELF mechanism

- The host build contains no business ELF and never invokes the sibling build.
- Build the app ELF independently, validate imports against the fixed ABI 1 exports in `main/host_abi.c`, and package it with `tools/module.py`.
- Boot validates the slot package, recovers the independent NVS journal, then relocates the selected module with Registry `elf_loader`.
- Each request exposes the validated request buffer through:

```c
const uint8_t *photoframe_host_png_data(void);
size_t photoframe_host_png_size(void);
void photoframe_host_report_result(int result);
```

- `esp_elf_request()` invokes the entry point. The result callback is mandatory because `elf_loader` 1.3.3 does not propagate an entry return value.
- `photoframe.so` is a deliverable but the current host does not load it.
- These are trusted native modules, not a sandbox. Authenticated `/api/module` staging, trial activation and rollback update business code without flashing the framework. A trial is confirmed only after a successful physical refresh; failure or reboot before confirmation returns to the active baseline.

The Flash contains bootloader data, a partition table, network NVS, one 5 MiB factory application, two 1 MiB ELF slots and a separate NVS journal. There is no filesystem, SD mount or whole-firmware OTA slot. See [module slots](../../../../docs-module-slots.md) for offsets, migration and confirmation semantics.

## Stable contracts

- Target: ESP32-S3 PhotoPainter N16R8, 7.3-inch 800×480 Spectra 6.
- ESP-IDF: exact commit `5e6f53cdb31fe5708eae3f55af9737be2822db22`.
- `elf_loader`: Registry dependency `^1.3.3`, resolved as 1.3.3; never fork or vendor it.
- Display pins: SCLK 10, MOSI 11, DC 8, CS 9, RST 12, BUSY 13.
- Accepted pixels: opaque black, white, yellow, red, blue, or green at exact channel values.
- Image endpoint: `POST /api/push`, raw PNG, maximum 5 MiB. Authenticated module management shares port 80 and the serialized operation lock.
- Missing or wrong Bearer token: 401. Unconfigured token: 503. Oversized body: 413. Invalid compatible input: 4xx without touching the panel.
- HTTP 200 is sent only after the display function returns success following final POWER_OFF BUSY completion.

## Security boundary

Keep Wi-Fi credentials and the device push token only in ignored host-local configuration or NVS. Keep CLIProxyAPI, CPAMP, OAuth, and other management secrets only on the PC. The device push token must be independent. Never expose the device port on the public Internet.
