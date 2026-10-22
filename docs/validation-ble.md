# BLE validation record

Device testing was explicitly authorized on 2026-10-22. The ESP32-S3 USB identity
was verified, the automatic Windows pusher/watchdog was stopped, and all 16 MiB
of Flash were backed up privately and verified before the first write. The user
confirmed that no microSD was installed. Each serial write targeted only the host
binary at `0x10000`; ROM digests before/after verified that the bootloader,
partition table, NVS, application slots and activation journal were unchanged by
those host writes. No provisioning or partition changes were performed.

The currently installed, device-tested host code is `fcf9d4e`; its 1104672-byte image SHA-256 is
`bef785931472e5c4888f3e9cf042fb0094a6a8e1d06c4dbd36375f7913a320b8`.
The build identifies as `fcf9d4e-dirty` because these validation notes were still
being edited; all firmware source changes were committed. Earlier radio tests
used `3c20e34` (1104144 bytes). The only subsequent firmware change adds read-only
uptime/reset diagnostics to authenticated status.
The pre-existing ABI 1 photoframe (version 15) and color-test entries were retained.

### Subsequent expiry correction — built, not yet installed

Code `40ea296` fixes a separate connection-lifetime defect. If a request itself
noticed idle/total expiry, it cleared the session before the worker's periodic
check; that check formerly returned false for a closed session and could omit
physical disconnection. Closed-session detection now remains true until reopen.
The NimBLE task checks the connection epoch before termination, immediately
revokes app authentication when closure is scheduled, and allows retry after a
transient HCI error.

The native regression failed on the old code at the closed-session assertion
and passes after the fix; all 86 host tests pass with the real application ELF.
The candidate binary is 1104864 bytes (+188592 from the fresh baseline), SHA-256
`cbe9c27f88ed4be2f70c46b406d453e307ef23715fa23088522e21c3b9c5c82f`.
Static DIRAM remains 145203 bytes. Its runtime heap/physical-disconnect behavior
still needs device validation. The build uses the pinned IDF and elf_loader;
the doctor reports only missing Linux Chrome as a failed prerequisite, which
does not affect this firmware build.

This candidate has **not** been flashed: the user has a pending manual USB driver
recovery step, so device writes wait for coordination. A follow-up stage-only
test on the installed image timed out during discovery before any package bytes
or mutations were sent. Unexpected POWERON resets remain a separate unresolved
observation; this protocol fix is not evidence that their cause is resolved.

## Local baseline

Source before changes: `5f1eaf5a8aa1ba84042587ae04f6bea1406b0ce5`.
Pinned IDF: `5e6f53cdb31fe5708eae3f55af9737be2822db22`, elf_loader 1.3.3.
A fresh `build-baseline` build was required because the existing build directory
contained paths from a different checkout. The local IDF had deleted Bluetooth
controller submodule files; a separate clean checkout was created from its Git
objects without modifying those local changes.

| Measurement | Baseline | Stage 1 | Installed ABI 2.1 | Installed change |
|---|---:|---:|---:|---:|
| Host `.bin`, bytes | 916272 | 1098448 | 1104672 | +188400 |
| Static DIRAM used, bytes | 116934 | 142922 | 145203 | +28269 |
| Linker DIRAM remaining, bytes | 224826 | 198838 | 196557 | -28269 |
| Runtime free internal RAM, online idle | 234775 | Not measured | 161807 | about -72968 |
| Runtime free PSRAM, online idle | 8310252 | Not measured | 8282952 | about -27300 |

The baseline table uses a fresh defaults build from `5f1eaf5`, with all shared
configuration values identical to the final build. The initial local-config
baseline was 919056 bytes (an additional unused RSA/DS option); comparing that
build gives +185616 bytes. Neither measurement includes the bootloader or apps.
For baseline heap measurement only, a temporary logging-only build (916464 bytes)
sampled internal/PSRAM heaps after releasing the status buffer. That instrumentation
was not committed. Both runtime measurements used the same original ABI 1 app,
no cached PNG, the same reachable Wi-Fi, and more than 60 seconds after boot.
The final BLE reading comes from an authenticated HTTP status query after reboot
into the original app (the inactive echo catalog entry was still present in this
sample). Small request/log
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
  scheduled service was not started during validation.
- The 5980-byte ABI 2.1 echo package was installed over BLE and explicitly
  activated/confirmed. Windows verified denied unauthenticated app reads, signed
  echo, direct custom GATT reads/writes, subscribed notifications and rejection
  of an old generation. Version 2 was also staged over BLE; activation, rollback
  to version 1 and restoration of the original photoframe were verified through
  the same manager via HTTP. The temporary echo application was removed.
- A valid **5242880-byte** six-color PNG returned **HTTP 200 in 50.82 seconds** on
  the original ABI 1 photoframe. Its global selection was confirmed and survived
  the next reboot. The temporary app service is absent and the permanent BLE
  service remains discoverable. Human confirmation of the image is still pending.
  Missing/wrong HTTP tokens returned 401; a deliberately oversized request
  returned 413 and an invalid PNG returned 422. The direct Windows HTTP probes
  did not use a proxy or follow redirects. A header-only test fixture initially
  timed out because this IDF parser dispatches a body-bearing request only after
  its first body callback; that fixture was corrected to send one invalid byte.
- Runtime diagnostics recorded unexpected `reset_reason=1` (POWERON) events.
  USB detach/re-enumeration coincided with failed large transfers. Attempts at a
  complete 1 MiB package were interrupted near 85%, 37% and 7%; none reached
  commit or altered the running version. Full-size sustained OTA reliability is
  **not yet validated**. Power/USB stability needs investigation; no software
  panic was captured by the filtered serial reader, which also lost its device.
- The original automatic pusher/watchdog remains stopped pending visual/network
  acceptance and power/USB diagnosis. Restoring the forced Windows USB sharing
  driver requires administrator rights; the ordinary process received Access
  Denied and a precise device-specific unbind command was provided to the user.

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
