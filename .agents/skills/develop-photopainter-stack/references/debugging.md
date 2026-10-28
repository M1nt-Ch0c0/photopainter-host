# Debug PhotoPainter safely

## Start with the doctor

From `photopainter-host`:

```bash
python3 tools/doctor.py --device-url http://DEVICE_IP/api/push
```

Use `--json` when another tool should consume the result. The doctor never reads `.env` or `secrets.env`, never prints secret environment values, and never sends a valid token.

Run `--probe-serial` only when interrupting the current device session is safe. Opening the ESP32-S3 native USB serial port can produce `USB_UART_CHIP_RESET` and abort an active push.

## Interpret layers separately

| Observation | Proven | Not proven |
|---|---|---|
| E-paper still shows an image | The panel retains pigment state | ESP32 has power, Wi-Fi, or a running application |
| USB serial device exists | ESP32 USB enumerated | Wi-Fi joined or HTTP works |
| Serial logs `wifi: IPv4` | DHCP completed | Host can reach port 80 |
| TCP port 80 opens | Network path and listener exist | HTTP handler returned a response |
| v2 GET /health returns 200, or retired control returns 410 | Device HTTP service is reachable | Pairing, a running APP or a full refresh |
| Historical v1 unauthenticated push returns 401 | Legacy configured HTTP service is reachable | A full refresh will complete |
| Historical v1 authenticated push returns 200 | Request, ELF display call, E6 refresh, and final POWER_OFF wait completed | Human-visible orientation and color correctness |
| Authenticated BLE image result is complete/OK | The same manager render call and final POWER_OFF wait completed | Human-visible orientation and color correctness |

Always request visual confirmation after a real refresh.

BLE image transfer does not need a Wi-Fi address. If its COMMIT reply is lost,
query the original request receipt after reconnecting instead of sending another
draw. Receipts are retained for four completed requests in RAM; a changed boot or
missing receipt leaves an ambiguous outcome that requires deliberate handling.
Pairing BID changes also prohibit using or redrawing the former owner's pending
transaction. Current pairing v2 is 尚未实机验证 until real button/reboot/reset tests
are recorded; simulated input and source tests do not count.

## USB and boot

Expected boot evidence includes:

- project `photopainter_host`;
- app version derived from the host Git revision;
- ESP-IDF `v6.0.3-1-g5e6f53cd` or the exact pinned commit;
- ELF loader 1.3.3;
- `photoframe payload ready`;
- the v2 read-only health listener after IPv4 assignment; old token routes are retired.

If USB exists but no logs appear, do not repeatedly open and close the port. Confirm no other monitor owns it, then perform one intentional reset while capturing the complete boot.

## Wi-Fi and HTTP

The device uses 2.4 GHz station mode with power saving disabled. Current v2 has a read-only health endpoint:

```bash
curl --max-time 5 --output /dev/null --write-out '%{http_code}\n' \
  http://DEVICE_IP/health
```

Expected results:

- 200 from `/health`: HTTP is online, not a display receipt;
- 410 from `/api/push`, `/api/module` or `/api/wifi`: expected v2 retirement;
- 401/503 from `/api/push`: only the old v1 configured/unconfigured meanings;
- timeout or code 000: wrong IP, routing failure, Wi-Fi loss, or an unpowered device.

Use the paired BLE client for authenticated status. Do not use an image request
as a liveness probe because it can refresh the panel.

### Windows Mobile Hotspot failure pattern

True-device testing observed periodic station disconnects with `reason:39`, sometimes followed by a ten-second association timeout. RSSI can remain strong and Wi-Fi power saving can already be disabled. When this occurs during a refresh, the panel may finish while the final HTTP response fails with `httpd_sock_err: error in send : 113`. The PC then cannot prove success and automatic retry can refresh repeatedly.

Use a stable 2.4 GHz access point for development and production. Do not treat repeated client retries as a fix. Stop the automatic pusher before a one-shot hardware test, preserve the first device log, and restart it only after the network can stay connected longer than the full display operation.

## PNG rejection

Before a hardware request, verify:

- PNG signature and no trailing data;
- exactly 800×480;
- non-interlaced;
- every pixel fully opaque;
- every pixel exactly one of `#000000`, `#ffffff`, `#ffff00`, `#ff0000`, `#0000ff`, or `#00ff00`;
- file size no greater than 5 MiB.

The component validates the whole image before touching the panel. A 4xx response is therefore not evidence of a display fault.

## ELF failures

If boot does not log `photoframe payload ready`:

1. Read paired BLE `status` and verify the active packaged slot and journal (`/api/module` is retired in v2).
2. Check package SHA, ELF validation, module ABI/version and interrupted trial recovery before rebuilding with the pinned IDF.
3. Confirm Registry `elf_loader` resolves to 1.3.3 and managed component files are unchanged.
4. Inspect undefined imports and ensure the fixed host ABI table supplies them.
5. Do not substitute `photoframe.so`; the host loads the independently packaged `photoframe.app.elf` from A/B data slots.

If the ELF reports an image error, reproduce it in native component tests before touching E6 code.

## Display failures

Distinguish these cases:

- no panel activity and immediate error: AXP2101/I2C/SPI/init failure;
- request remains active: E6 stage or BUSY wait in progress;
- `PHOTOFRAME_ERR_BUSY_TIMEOUT`: BUSY remained low for 120 seconds;
- response send fails after display work: physical completion is not proven until the original signed receipt is recovered, even if the panel visibly changed.

Keep SCLK 10, MOSI 11, DC 8, CS 9, RST 12, and BUSY 13 unchanged. Do not experiment with power rails or display timing without explicit hardware scope and a recovery plan.
