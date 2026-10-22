# BLE validation record

Hardware has not been accessed: no serial connection, flashing, provisioning,
BLE scan/connection or physical push is part of these development checks.

## Local baseline

Source before changes: `5f1eaf5a8aa1ba84042587ae04f6bea1406b0ce5`.
Pinned IDF: `5e6f53cdb31fe5708eae3f55af9737be2822db22`, elf_loader 1.3.3.
A fresh `build-baseline` build was required because the existing build directory
contained paths from a different checkout. The local IDF had deleted Bluetooth
controller submodule files; a separate clean checkout was created from its Git
objects without modifying those local changes.

| Measurement | Baseline | Stage 1 | Change |
|---|---:|---:|---:|
| Host `.bin`, bytes | 919056 | 1098448 | +179392 |
| Static DIRAM used, bytes | 116934 | 142922 | +25988 |
| Linker DIRAM remaining, bytes | 224826 | 198838 | -25988 |
| Runtime free internal RAM | Not measured | Not measured | Requires hardware |
| Runtime free PSRAM | Not measured | Not measured | Requires hardware |

Linker remaining space is **not free runtime heap**. NimBLE/controller tasks,
PSRAM pools, loaded ELF and caches allocate at runtime. Read `free_internal_ram`,
`minimum_free_internal_ram`, `free_psram`, `minimum_free_psram`, and `cache_bytes`
from authenticated status at comparable boot/idle/upload/display points to
measure the actual changes. Baseline firmware did not expose internal heap;
capture an authorized baseline instrumented build if that comparison is needed.

The baseline Python suite ran 81 tests (one optional application ELF case skipped).
The new protocol suite tests actual C with ASan/UBSan: independent HMAC vectors,
wrong/absent token, tampering, replay, sequence exhaustion, unauthenticated commands,
busy/OOM, begin/repeat begin, duplicate/conflicting/out-of-order chunks, bounds,
truncated commit, package SHA, progress, explicit activation, manager error,
disconnect, whole-package retransmission, expiry and fragmented ATT writes.

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
