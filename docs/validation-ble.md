# BLE validation record

The initial development checks used no hardware. The user subsequently authorized
device testing in this session. A native Windows adapter scan has completed
successfully (no PhotoPainter advertisement from the old firmware); ESP USB
identity has been confirmed after reconnect. Full Flash backup and device-level
checks are in progress. No host flash or provisioning has been performed yet.

## Local baseline

Source before changes: `5f1eaf5a8aa1ba84042587ae04f6bea1406b0ce5`.
Pinned IDF: `5e6f53cdb31fe5708eae3f55af9737be2822db22`, elf_loader 1.3.3.
A fresh `build-baseline` build was required because the existing build directory
contained paths from a different checkout. The local IDF had deleted Bluetooth
controller submodule files; a separate clean checkout was created from its Git
objects without modifying those local changes.

| Measurement | Baseline | Stage 1 | Including ABI 2.1 | Final change |
|---|---:|---:|---:|---:|
| Host `.bin`, bytes | 919056 | 1098448 | 1104144 | +185088 |
| Static DIRAM used, bytes | 116934 | 142922 | 145050 | +28116 |
| Linker DIRAM remaining, bytes | 224826 | 198838 | 196710 | -28116 |
| Runtime free internal RAM | Not measured | Not measured | Pending device test | Not yet known |
| Runtime free PSRAM | Not measured | Not measured | Pending device test | Not yet known |

Linker remaining space is **not free runtime heap**. NimBLE/controller tasks,
PSRAM pools, loaded ELF and caches allocate at runtime. Read `free_internal_ram`,
`minimum_free_internal_ram`, `free_psram`, `minimum_free_psram`, and `cache_bytes`
from authenticated status at comparable boot/idle/upload/display points to
measure the actual changes. Baseline firmware did not expose internal heap;
capture an authorized baseline instrumented build if that comparison is needed.
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

## Hardware checklist — needs separate authorization

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
