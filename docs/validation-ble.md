# BLE validation record

Device testing was explicitly authorized on 2026-10-22. The ESP32-S3 USB identity
was verified, the automatic Windows pusher/watchdog was stopped, and all 16 MiB
of Flash were backed up privately and verified before the first write. The user
confirmed that no microSD was installed. Each serial write targeted only the host
binary at `0x10000`; ROM digests before/after verified that the bootloader,
partition table, NVS, application slots and activation journal were unchanged by
those host writes. No provisioning or partition changes were performed.

The currently installed host code is `40ea296`; its 1104864-byte image SHA-256 is
`cbe9c27f88ed4be2f70c46b406d453e307ef23715fa23088522e21c3b9c5c82f`.
It was written through the restored native Windows COM55 port after checking
the exact ESP32-S3 MAC and rechecking the full backup hash. Image readback and
both regions outside factory matched. Filtered boot output identifies `40ea296`;
the saved network and original application came up normally. Earlier radio tests
used `3c20e34` (1104144 bytes) and `fcf9d4e` (1104672 bytes, adding read-only
uptime/reset diagnostics). The latter build identified as `fcf9d4e-dirty` because
validation notes were being edited; all firmware source was committed.
The pre-existing ABI 1 photoframe (version 15) and color-test entries were retained.
The computer executable contains production changes through `dab1adc`; subsequent
computer commits add validation/tests/docs, with the final hardware probes at
`655d30a`. The mirrored SDK, BLE echo and application builds use `photoframe`
commit `d2dd482`. Branches in all four repositories are
`codex/ble-discovery-app-ota`.

### Subsequent expiry correction

Code `40ea296` fixes a separate connection-lifetime defect. If a request itself
noticed idle/total expiry, it cleared the session before the worker's periodic
check; that check formerly returned false for a closed session and could omit
physical disconnection. Closed-session detection now remains true until reopen.
The NimBLE task checks the connection epoch before termination, immediately
revokes app authentication when closure is scheduled, and allows retry after a
transient HCI error.

The native regression failed on the old code at the closed-session assertion
and passes after the fix; all 86 host tests pass with the real application ELF.
The installed binary is 1104864 bytes (+188592 from the fresh baseline).
Static DIRAM remains 145203 bytes. The build uses the pinned IDF and elf_loader;
the doctor reports only missing Linux Chrome as a failed prerequisite, which
does not affect this firmware build.

The user completed the device-specific Windows USB driver recovery; USBIP is
unbound and subsequent tests use the native Windows driver. Before this update,
a follow-up stage-only test timed out during discovery before any package bytes
or mutations were sent. Unexpected POWERON resets remain a separate observation:
the user confirmed there was no cable movement, manual reset or restart at those
times. The protocol correction alone does not establish their cause.

On the native Windows driver, the idle-expiry hardware test observed physical
disconnection after 59.9 seconds while retaining all client objects. A fresh
connection authenticated successfully, the 1 MiB receive buffer was freed and
manager admission could be acquired and aborted again. Uptime increased from
60459 to 131639 ms with reset reason 11 unchanged (the deliberate serial reset
for this flash). No application was staged or activated by this test.

## Local baseline

Source before changes: `5f1eaf5a8aa1ba84042587ae04f6bea1406b0ce5`.
Pinned IDF: `5e6f53cdb31fe5708eae3f55af9737be2822db22`, elf_loader 1.3.3.
A fresh `build-baseline` build was required because the existing build directory
contained paths from a different checkout. The local IDF had deleted Bluetooth
controller submodule files; a separate clean checkout was created from its Git
objects without modifying those local changes.

| Measurement | Baseline | Stage 1 | Installed ABI 2.1 | Installed change |
|---|---:|---:|---:|---:|
| Host `.bin`, bytes | 916272 | 1098448 | 1104864 | +188592 |
| Static DIRAM used, bytes | 116934 | 142922 | 145203 | +28269 |
| Linker DIRAM remaining, bytes | 224826 | 198838 | 196557 | -28269 |
| Runtime free internal RAM, online idle | 234775 | Not measured | 161815 | about -72960 |
| Runtime free PSRAM, online idle | 8310252 | Not measured | 8282936 | about -27316 |

The baseline table uses a fresh defaults build from `5f1eaf5`, with all shared
configuration values identical to the final build. The initial local-config
baseline was 919056 bytes (an additional unused RSA/DS option); comparing that
build gives +185808 bytes. Neither measurement includes the bootloader or apps.
For baseline heap measurement only, a temporary logging-only build (916464 bytes)
sampled internal/PSRAM heaps after releasing the status buffer. That instrumentation
was not committed. Both runtime measurements used the same original ABI 1 app,
no cached PNG, the same reachable Wi-Fi, and more than 60 seconds after boot.
The final BLE reading comes from an authenticated HTTP status query after reboot
into the original app, with the temporary echo catalog entry removed. Small request/log
allocations mean these deltas describe the observed operating footprint, not an
isolated allocator accounting of every BLE byte.

Linker remaining space is **not free runtime heap**. NimBLE/controller tasks,
PSRAM pools, loaded ELF and caches allocate at runtime. Read `free_internal_ram`,
`minimum_free_internal_ram`, `free_psram`, `minimum_free_psram`, and `cache_bytes`
from authenticated status at comparable boot/idle/upload/display points to
measure the actual changes. Baseline firmware did not expose internal heap;
the authorized temporary instrumentation above supplied that comparison.
Heap counters are sampled at status-query time, so a BLE receive buffer is
observable even though the manager has not yet executed a stage job.

The baseline Python suite ran 81 tests (one optional application ELF case skipped).
The new protocol suite tests actual C with ASan/UBSan: independent HMAC vectors,
wrong/absent token, tampering, replay, sequence exhaustion, unauthenticated commands,
busy/OOM, begin/repeat begin, duplicate/conflicting/out-of-order chunks, bounds,
truncated commit, package SHA, progress, explicit activation, manager error,
disconnect, whole-package retransmission, expiry and fragmented ATT writes.

## Completed local checks

- Host: 86 tests pass with the real `photoframe.app.elf` integration enabled,
  including catalog/boot recovery, ABI 1/2.0/2.1 dispatch, copied BLE resources,
  bounded queues, manager admission, synchronous app writes and unload cleanup.
- `photoframe`: PNG native test and actual BLE echo entry test pass; default
  `.app.elf`, legacy `.so` and `ble_echo.app.elf` build with the pinned IDF and
  unmodified Registry elf_loader 1.3.3. The echo ELF is 1884 bytes; package is
  5980 bytes. Header and import-list SDK copies compare byte-for-byte.
- Computer: Go 1.26.0 `make check` passes (unit/race/vet/static build), including
  shared HMAC vectors, transfer and retry behavior, application generation
  binding and scan cancellation. Windows amd64 cross-build succeeds. A macOS
  arm64 CGO-disabled HTTP-only fallback build succeeds; native macOS BLE still
  requires a macOS CGO build and radio validation. Chrome-specific tests skip
  when Chrome is unavailable in the Linux build environment.
- `partitions.csv`, generated partition-table bytes, `dependencies.lock`, the
  existing HTTP server and slot installer are unchanged from the baseline.

## Authorized device results

- Discovery, mutual HMAC and paged status work on Windows 11 build 26100.
  The device advertised offline state before the saved AP became available,
  then advertised the acquired IP and the matching truncated SSID hash. No
  Wi-Fi credentials or token were broadcast, printed, or changed.
- A wrong token was rejected; an unauthenticated client could read discovery
  but could not query the application catalog. A replayed signed request was
  rejected and the next valid sequence still worked.
- Identical duplicate chunks were accepted; gaps and conflicting chunks were
  rejected. Activation during reception returned BLE busy; HTTP push returned
  409 through the same manager admission. Abort released admission. A bad
  whole-package SHA returned the hash error without changing the app catalog.
- A deliberate client exit at 10% of a 1 MiB upload released the receive buffer:
  PSRAM returned from about 7234356 to 8282936 bytes. The original running app
  and both original catalog entries were unchanged. Subsequent complete small
  packages were accepted, explicitly activated and rolled back. No persistent
  partial-install resume is promised.
- Negotiated ATT MTU is 247 (244-byte write capacity). During a new 1 MiB upload,
  free internal RAM stayed near 162695 bytes, while PSRAM moved from 8282800 to
  7234220 bytes: the receive allocation is in PSRAM, not internal RAM.
- Device binding saved only the public ID in a separate Windows user state file;
  the user's `.env` was not opened or changed. Native Windows connection issues
  discovered by these checks led to explicit WinRT resource ownership and an
  upstream scan-parser fix in the computer client; see its BLE document.
  Two consecutive native authenticated discoveries finished in 18.3 and 15.5
  seconds, both inside the background worker's 30-second deadline and both
  returned the current reachable push target. The tested Windows executable
  (SHA-256 `7b137c5912fb31bd7c27cd80110f5dd440c5028bd5b4fd72c1dbf543540e3257`)
  replaced the deployed executable after preserving its predecessor; the
  scheduled service remained stopped through deliberate radio/display tests
  and was restored after the independent-power comparison below.
- The 5980-byte ABI 2.1 echo package was installed over BLE and explicitly
  activated/confirmed. Windows verified denied unauthenticated app reads, signed
  echo, direct custom GATT reads/writes, subscribed notifications and rejection
  of an old generation. Version 2 was also staged over BLE; activation, rollback
  to version 1 and restoration of the original photoframe were verified through
  the same manager via HTTP. The temporary echo application was removed.
- A valid **5242880-byte** six-color PNG returned **HTTP 200 in 50.82 seconds** on
  the original ABI 1 photoframe. Its global selection was confirmed and survived
  the next reboot. The temporary app service is absent and the permanent BLE
  service remains discoverable. The user confirmed the expected left-to-right
  black/white/yellow/red/blue/green bands, upper-left white square, orientation
  and colors. Physical display acceptance is complete.
  Missing/wrong HTTP tokens returned 401; a deliberately oversized request
  returned 413 and an invalid PNG returned 422. The direct Windows HTTP probes
  did not use a proxy or follow redirects. A header-only test fixture initially
  timed out because this IDF parser dispatches a body-bearing request only after
  its first body callback; that fixture was corrected to send one invalid byte.
- Runtime diagnostics recorded unexpected `reset_reason=1` (POWERON) events.
  USB detach/re-enumeration coincided with failed large transfers. Attempts at a
  complete 1 MiB package were interrupted near 85%, 37% and 7%; none reached
  commit or altered the running version. These computer-USB runs did not validate
  sustained full-size OTA. Power/USB stability needed investigation; no software
  panic was captured by the filtered serial reader, which also lost its device.
- The follow-up on host `40ea296`, with the native Windows USB driver restored,
  passed authentication, replay, duplicate/gap/conflict, hash and HTTP 409 checks
  again, but its 1 MiB upload stopped at 41% (last progress 430000 bytes at 232 s).
  HTTP telemetry went from uptime 418006 ms/reset reason 11 to uptime 10418 ms/
  reset reason 1. The filtered native serial monitor ended with `SerialException`,
  without a captured panic, watchdog or brownout message. This rules out a
  USBIP-only explanation. The fixed IDF maps reason 1 specifically from the ROM
  power-on reset cause; software, watchdog, brownout and USB resets have separate
  mappings. It still does not identify whether supply, reset line or another
  hardware condition caused the event. No candidate was committed; the original
  apps remained selected and their versions unchanged.
- The original automatic pusher/watchdog was held stopped during power/USB
  diagnosis. Visual acceptance is complete. The
  user completed the administrator-only unbind operation; native COM55 was
  verified and no forced USBIP binding remains. A different cable and direct
  computer USB port were requested as a physical comparison before another
  sustained transfer.

### Power-path comparison

The user replaced the data cable and moved from USB bus `1-13` to direct port
`1-12`. Windows still identified the same ESP32-S3 MAC on native COM55, with no
USBIP binding. With that connection, repeated POWERON events interrupted the
preliminary transfer checks before the maximum-package phase; the filtered
serial reader again ended with `SerialException`.

A second comparison closed the serial reader and temporarily disabled **only
the current power plan's AC USB selective suspend**. Windows reported AC power.
The upload still failed at 13% (136400 bytes at 76.3 s), and uptime fell from
136974 to 18831 ms with reason 1. The test runner restored the original AC
setting in `finally`, and a separate query verified it was enabled again;
the DC setting was never changed. Thus neither cable/port replacement nor this
USB power-policy/serial-reader comparison resolved the resets.

The user then moved the device to an independent USB power adapter, disconnected
it from the computer USB and confirmed that **no battery is installed**. Windows
reported zero PhotoPainter serial ports. The firmware and 1 MiB package remain
unchanged for this comparison; BLE performs the upload while authenticated HTTP
status supplies uptime and heap samples. No PMIC register or radio-power change
was made. The board's [official schematic](https://files.waveshare.com/wiki/ESP32-S3-PhotoPainter/ESP32-S3-PhotoPainter-Schematic.pdf)
was reviewed to distinguish the external USB supply, onboard power management
and reset path; it does not by itself establish which component caused a reset.

**The independent-power comparison passed the full 1 MiB hardware test.**
All 1048576 bytes were acknowledged in 554.5 seconds (about 9 min 15 s), followed
by successful SHA/envelope/ELF validation and staging through the existing
installer. The candidate was explicitly pending, the original running app was
unchanged, and the test removed the candidate without activation. The complete
test, including authentication/replay/chunk/error/HTTP 409 checks, took 651.02 s.
Uptime advanced from 116919 to 696309 ms with no reset; both original application
entries and their versions remained unchanged.

During reception, internal free RAM stayed near 162 KB and free PSRAM near
7234356 bytes. After staging and candidate removal, PSRAM returned to 8282800
bytes through BLE status (8282936 through HTTP after connection cleanup).
Minimum free internal RAM was 76796 bytes and minimum PSRAM was 6185640 bytes,
including staging's existing installer allocations. This is one completed
maximum-size run, not a long-duration endurance claim.

Immediately after the upload, two fresh authenticated Windows discoveries
completed in 5.2 and 8.7 seconds, returning the reachable device push address.
Both were inside the unchanged 30-second background-discovery deadline.

The outcome supports using the present independent power arrangement for
continued operation. It does **not** identify the failing computer port, cable,
power rail or reset component, and computer-USB stability is not certified.

### Restored deployment

After the maximum-package test and consecutive discovery checks passed, the
existing `AiQuotaFrame` scheduled task was restarted. Windows reported one
`ai-quota-frame` service process and its established outbound connection to the
device's port 80. Authenticated device status then reported the original
photoframe version 15 selected/loaded, `runtime_ready=true`, `confirmed=true`,
`display_completed=true`, no pending candidate and no custom test service.
Uptime was 860569 ms with no additional reset. This verifies restored operation;
the separately observed HTTP 200 plus human image acceptance remains the
deliberate six-color display test recorded above. Service logs were not opened.

The current deployment retains independent USB power with no battery. There is
no computer serial attachment, no new listening port, and the watchdog still
checks process existence. The user's environment files were not modified.

## Hardware checklist for remaining cases and future runs

1. Stop the automatic pusher and watchdog; identify the exact device/port.
   Before any flash write, back up **all 16 MiB**, verify readback and SHA-256;
   back up installed microSD unless explicitly waived. Keep backups private.
2. Write only the approved host binary at `0x10000`. Preserve partition table,
   application slots, activation journal and NVS. Do not run a broad flash target.
3. Verify boot protection, offline local START, the function key and ABI 1/2 apps.
   Verify discovery while Wi-Fi is unconfigured, SD JSON is invalid and AP is absent.
4. On Windows, then macOS, scan/read the public record; authenticate with the
   independent token through process environment. A bad token must not permit
   reading application status, writes or activation. Never capture token logs.
5. Change Wi-Fi/hotspot and inspect updated state/IP/hash. A verified new IP must
   update the computer's in-memory target, leaving `.env` unchanged. With client
   isolation or no IP route, HTTP still fails; BLE package management must work.
6. Install a known package at MTU 23 and negotiated larger MTU; compare progress
   and pending version. Interrupt at several receive offsets; verify global
   admission is released and whole-package retransmission succeeds. Corrupt a
   chunk/hash and confirm no candidate is installed. Verify timeout cleanup.
7. Exercise HTTP push, Wi-Fi save and the button during BLE upload: expect 409/
   ignored button, no queued surprise refresh. During an HTTP physical refresh,
   BLE begin/activate must return busy without blocking the BLE radio task.
8. Explicitly activate a valid candidate, test a bad START candidate and rollback,
   and reboot before confirmation. Confirm original A/B and boot recovery rules.
   ABI 1 confirmation still needs its deliberate valid PNG refresh.
9. Check wrong/missing HTTP token 401, busy 409, oversize 413, invalid PNG 4xx.
   A valid push requires HTTP 200 **and human confirmation** of orientation/colors.
10. Record the four heap statistics at idle, during upload, after disconnect,
    while an ELF/cache is loaded and after a 5 MiB HTTP reception. Repeat combined
    Wi-Fi traffic/BLE operations to check coexistence, stack margins and leaks.
11. Install/activate `ble-echo`, write/read `hello`, check notification subscription,
    disconnect/reconnect, then switch away and confirm its GATT service and old
    generation no longer work. Permanent discovery/upload must remain available.
    Repeat switches to look for dynamic GATT/resource leaks; try unauthorized,
    oversized and stale-revision writes and verify no application invocation.
