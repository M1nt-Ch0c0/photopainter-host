# Offline BLE image validation — 2026-10-23

**Route changed after this test:** the user now requires the PNG receive protocol
to live in the photoframe ABI 2.1 application, using generic host BLE/auth/display
services. Further extension or serial flashing of the dedicated host PNG path
is paused. Preserve this implementation and its evidence as the existing baseline;
it is not completion of the new app-owned route. See `esp32s3/MAC_BLE_APP_HANDOFF.md`.

This extends the [original BLE validation](validation-ble.md). The user explicitly
authorized Mac hardware testing and the one-time host update. The device has no
microSD or battery. All radio tests below use independent USB power, disconnected
from the Mac USB data port, and an authenticated device IP of `0.0.0.0`.

## Build and safe deployment

- Host code: `b4709c0`, branch `codex/ble-image-push`.
- Computer code: `0e94fab`, same branch; Go 1.26.0, macOS 26.3.1 arm64, native CGO.
- App SDK/example source: `photoframe d2dd482`; no app binary change is required
  for BLE images. The original ABI 1 `photoframe v15` is retained.
- Pinned ESP-IDF `5e6f53cdb31fe5708eae3f55af9737be2822db22` and unmodified Registry
  elf_loader 1.3.3. Partition table and device credentials unchanged.
- Host bin: 1106048 bytes; SHA-256
  `17a546d0022f8a6b3fcab48bd22b3c38cd95e954b8dbf83f2581d62d6786ef63`.
- Mac executable SHA-256:
  `8c9251a94b7564465ccf897c78faafd9ad4ae3f825d9d50b1cf5f439645f1bc9`.

Before writing, all 16 MiB Flash were backed up privately in 256 KiB chunks,
checking device MD5 for every chunk and for the complete read, then recording
SHA-256. An earlier single-stream backup failed before any write. Only the host
bin at `0x10000` was written. Image verification passed, and ROM digests of both
regions outside factory matched before/after. Filtered boot evidence identifies
`b4709c0` and the original v15 app. No partition write, provisioning or host OTA
was performed. Raw backups and credentials remain outside Git.

## Local checks

- Host: 86 tests pass, including real ELF integration, actual protocol C under
  ASan/UBSan, manager admission, input lifetime and existing A/B/boot recovery.
- Computer: Linux and Mac `make check` pass (unit, race, vet, Chrome rendering and
  build). Windows amd64 cross-build passes.
- New tests cover PNG envelope/CRC/palette/size validation, signed receipts,
  lost replies, late native completion after cancellation, changed boot, missing
  or mismatched receipts, permanent failure and no HTTP fallback in BLE mode.

## Offline application update

With automatic pushers stopped, BLE installed a 5980-byte `ble-echo v1` package
in the previously absent test catalog entry and explicitly activated it. Signed
status confirmed v1, then v2 after a second install/activate, then v1 after
rollback. A custom ABI 2.1 write/read echoed `mac-offline-ble` correctly.
Switching back to `photoframe v15` removed the custom GATT service. Original
photoframe A/B versions 15/16 and color-test 1/3 were preserved.

## One-refresh image and lost-reply test

`TestHardwareOfflineBLEImage` passed on the Mac in **55.61 seconds**. It requires
no device IPv4 on every connection and uses the app identity's real CoreBluetooth
access. The private trusted launcher supplies credentials without command-line
tokens. The production executable was temporarily replaced by the opt-in test
binary while launchd was unloaded; its original hash was verified after restore.

1. Reserve a full 5 MiB PNG buffer in PSRAM, send 400 bytes, then disconnect before
   COMMIT. Fresh authentication confirms memory is released and the app unchanged.
2. Upload an eight-byte invalid PNG with a matching SHA-256. The shared decoder
   rejects it with code 12; application readiness/version are unchanged.
3. Upload a real 12915-byte quota PNG. Upload reaches 100% in **8.9 seconds**.
4. Complete the physical refresh, deliberately discard its successful COMMIT
   reply, reconnect and query the same request receipt. Signed completion is
   recovered in **41.4 seconds** from image-operation start.
5. Assert exactly **one COMMIT and one receipt query**, no repeat draw, v15 still
   running, runtime readiness true, and no device reboot.

The initial fixture used Mac's unset theme and therefore rendered the classic
layout. The user identified this display preference regression. The original
Windows configuration uses `mint-skin` with CLAUDE/CODEX/GROK; Mac configuration
was corrected to those values and the original 15-minute interval. This changes
the computer-rendered image, not the device application version. The initial
fixture is transport evidence, not acceptance of the desired final appearance.

A separate 59124-byte mint-skin image subsequently returned an authenticated BLE
physical-completion receipt. The user confirmed the restored theme, three provider
rows, orientation and colors were correct. The Mac background pusher remains
disabled/unloaded because the user then changed the implementation route.

## Memory

| Measurement | Prior BLE host `40ea296` | Image host `b4709c0` | Change |
|---|---:|---:|---:|
| Host bin bytes | 1104864 | 1106048 | +1184 |
| Static DIRAM bytes | 145203 | 145587 | +384 |
| Linker DIRAM remaining | 196557 | 196173 | -384 |

Relative to the fresh pre-BLE baseline (916272 bytes), the new host is 189776
bytes larger. Linker figures are not runtime heap. The previous runtime baseline
was online; the new offline readings below are not a controlled before/after
measurement of the small image extension alone.

| Runtime point | Free internal RAM | Free PSRAM | Uptime ms |
|---|---:|---:|---:|
| Before image checks, v15 loaded | 163563 | 8282800 | 592669 |
| During 5 MiB receive | 163563 | 3039916 | 594149 |
| After partial disconnect | 163735 | 8284376 | 596849 |
| After image and receipt recovery | 163459 | 8282800 | 643069 |

The 5 MiB reservation consumes 5242884 PSRAM bytes and no measured internal heap.
After the complete test PSRAM returns to the pre-test reading; internal RAM
differs by 104 bytes. Observed minima are 78780 internal and 3037756 PSRAM bytes.
Reset reason stays 1 and uptime increases throughout.

## Limits and further checks

The test reserves the maximum image buffer but sends an ordinary rendered PNG;
it does not claim a complete 5 MiB radio image transfer or long-term endurance.
Prior Windows HTTP/radio checks remain in the original validation record; this
offline Mac run does not repeat HTTP reachability tests. No display driver,
decoder ABI, partition or GPIO behavior changed.

Receipts are a bounded, four-entry per-boot RAM cache. Changed boot, missing or
mismatched receipt must stop automatic repetition of the ambiguous image.
There is no durable exactly-once guarantee across computer process restarts.
Images and packages remain observable over the unencrypted BLE link; HMAC
protects authentication/integrity and the token is never transmitted.
