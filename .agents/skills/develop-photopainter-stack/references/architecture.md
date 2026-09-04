# PhotoPainter stack architecture

## Repository ownership

| Repository | Owns | Does not own |
|---|---|---|
| `photopainter-host` | ESP32 boot, NVS configuration, Wi-Fi station, Bearer authentication, HTTP request buffering, ELF relocation and invocation | PNG decoding policy, E6 implementation, quota collection |
| `photoframe` | Strict PNG decoding, six-color validation, 180-degree packing, AXP2101 power setup, Spectra 6 E6 refresh, `.app.elf` and `.so` artifacts | Wi-Fi, HTTP, secrets, scheduling |
| `ai-quota-frame` | Quota collection, 800×480 six-color rendering, active HTTP push, latest-frame queue and retries | Device firmware, inbound HTTP service, device provisioning |
| `esp32s3` | Links to the three repositories | Source code and build artifacts |

Keep the repositories as siblings for the default host build:

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
  embedded photoframe.app.elf
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

- The host build invokes `elf_embed_binary()` on the sibling `photoframe` project.
- The build produces `photoframe.app.elf`, generates the required host symbol table, and embeds the ELF bytes in the factory application.
- Boot calls `esp_elf_init()`, registers generated symbols, and relocates the embedded ELF.
- Each request exposes the validated request buffer through:

```c
const uint8_t *photoframe_host_png_data(void);
size_t photoframe_host_png_size(void);
void photoframe_host_report_result(int result);
```

- `esp_elf_request()` invokes the entry point. The result callback is mandatory because `elf_loader` 1.3.3 does not propagate an entry return value.
- `photoframe.so` is a deliverable but the current host does not load it.
- This is runtime relocation, not a sandbox and not runtime upgrade. Replacing the component currently requires rebuilding and flashing the host.

The Flash contains bootloader data, a partition table, NVS, PHY data, and one 5 MiB factory application. There is no filesystem, SD mount, separate ELF partition, OTA slot, or rollback slot.

## Stable contracts

- Target: ESP32-S3 PhotoPainter N16R8, 7.3-inch 800×480 Spectra 6.
- ESP-IDF: exact commit `5e6f53cdb31fe5708eae3f55af9737be2822db22`.
- `elf_loader`: Registry dependency `^1.3.3`, resolved as 1.3.3; never fork or vendor it.
- Display pins: SCLK 10, MOSI 11, DC 8, CS 9, RST 12, BUSY 13.
- Accepted pixels: opaque black, white, yellow, red, blue, or green at exact channel values.
- Endpoint: only `POST /api/push`, raw PNG, maximum 5 MiB.
- Missing or wrong Bearer token: 401. Unconfigured token: 503. Oversized body: 413. Invalid compatible input: 4xx without touching the panel.
- HTTP 200 is sent only after the display function returns success following final POWER_OFF BUSY completion.

## Security boundary

Keep Wi-Fi credentials and the device push token only in ignored host-local configuration or NVS. Keep CLIProxyAPI, CPAMP, OAuth, and other management secrets only on the PC. The device push token must be independent. Never expose the device port on the public Internet.
