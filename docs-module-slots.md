# Independent ELF modules

> 当前宿主已扩展为 5 应用独立 A/B；Wi-Fi、应用 ID、迁移和管理命令以 [新指南](docs-multi-apps.md) 为准。本文保留旧单应用包和首次种子部署的说明。

The host firmware builds without a sibling checkout and contains no business ELF.
`main/host_abi.c` owns ABI version 1's fixed host exports; an independent module is
built with the same pinned ESP-IDF commit and the app ELF profile. Registry
`elf_loader` remains unmodified at 1.3.3. This is relocation of trusted native
code, not a sandbox. Only authenticated operators should install modules.

## Flash layout

| Partition | Offset | Size | Purpose |
|---|---|---|---|
| nvs | 0x9000 | 24 KiB | Wi-Fi and push token |
| factory | 0x10000 | 5 MiB | Framework firmware |
| elf_a | 0x510000 | 1 MiB | Module slot A |
| elf_b | 0x610000 | 1 MiB | Module slot B |
| elf_state | 0x710000 | 24 KiB | Independent NVS activation journal |

Provisioning network credentials does not erase the module journal. On a fresh
installation slot A must contain a valid packaged module. Flash the partition
table, framework, network NVS and A package during initial migration, after the
verified full-device backup. Erase the B slot and module journal for that first
installation only. Subsequent framework flashes must preserve the slots/journal.

### First installation or migration from embedded ELF

These commands replace the partition table and initialize both module slots.
Use them only after identifying the correct device, obtaining flash authorization,
stopping automatic pushers, and making a verified full 16 MiB backup. They are
not the routine business-update procedure. Set `PORT` to the discovered serial
port and `BACKUP_DIR` to a private directory outside every repository.

```sh
# In the pinned IDF environment, from photopainter-host:
python -m esptool --chip esp32s3 --port "$PORT" read-flash \
  0 0x1000000 "$BACKUP_DIR/flash-original.bin"
python -m esptool --chip esp32s3 --port "$PORT" verify-flash \
  0 "$BACKUP_DIR/flash-original.bin"
# Record a SHA-256 digest as well; use sha256sum on Linux.
shasum -a 256 "$BACKUP_DIR/flash-original.bin" > "$BACKUP_DIR/SHA256SUMS"

# Build photoframe.app.elf separately using the command in the next section.
idf.py build
python tools/module.py package --input ../photoframe/build-app/photoframe.app.elf \
  --output build/module-seed.pkg --version 1
# Fill the ignored secrets.env locally first. The tool does not display values.
python tools/provision.py --config secrets.env \
  --generate-only "$BACKUP_DIR/new-config.nvs.bin"

# FIRST MIGRATION ONLY: A + B + independent journal, 0x510000..0x715fff.
python -m esptool --chip esp32s3 --port "$PORT" --after no-reset \
  erase-region 0x510000 0x206000
python -m esptool --chip esp32s3 --port "$PORT" write-flash \
  --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x9000 "$BACKUP_DIR/new-config.nvs.bin" \
  0x10000 build/photopainter_host.bin \
  0x510000 build/module-seed.pkg
```

Back up an installed microSD unless explicitly waived for the run. A blank seed
or missing A package cannot serve images; `idf.py flash` alone does not install
the independent business ELF. After boot, check an unauthenticated push returns
401, then check module status and perform one valid image push. Only start the
automatic service after HTTP 200 and visual verification. Install a second
validated module through the normal update flow to establish a working fallback.

For a later framework-only update with this exact partition layout, write only
`0x10000 build/photopainter_host.bin`; do not erase or rewrite the module journal
or either ELF slot. Restore the full backup only as an explicit recovery action:
it also restores the old application, layout and credentials.

## Package and update

A package consists of a 4096-byte header sector followed by the ELF. The first
52 bytes are little-endian magic `0x31464c45`, format 1, ABI 1, payload length,
uint32 module version and SHA-256 of the ELF. The rest of the header is padding.
The device checks the SHA, ELF32/Xtensa/ET_DYN identity, table/string/section and
relocation bounds before loading. Host exports remain fixed across business
updates; adding an import requires a compatible host ABI change.

```sh
# From photoframe, in the pinned IDF environment:
idf.py -B build-app -DIDF_TARGET=esp32s3 -DPHOTOFRAME_ARTIFACT=app build

# From photopainter-host:
python3 tools/module.py package --input ../photoframe/build-app/photoframe.app.elf \
  --output build/module.pkg --version 10
python3 tools/module.py status --url http://DEVICE_IP
python3 tools/module.py stage --url http://DEVICE_IP --input build/module.pkg
python3 tools/module.py activate --url http://DEVICE_IP
python3 tools/module.py push --url http://DEVICE_IP --input frame.png
python3 tools/module.py status --url http://DEVICE_IP
```

Stop the automatic service before activating a candidate so its first physical
refresh is deliberate. Inspect status after each step; `0` means A, `1` means B,
and `-1` means absent. `active` is the confirmed baseline, `previous` the older
confirmed fallback, `pending` the candidate, and `trial` indicates unconfirmed
activation. `loaded`, `version` and `ready` describe the currently relocated
module. During a trial, `loaded` can differ from `active`.

```sh
# Discard pending/trial work, or switch to the previous confirmed version:
python3 tools/module.py rollback --url http://DEVICE_IP
python3 tools/module.py status --url http://DEVICE_IP
```

Staging returns 202; malformed packages return 422, oversize packages 413,
concurrent operations or rejected state transitions 409, and incomplete uploads
408. A management 200 proves loading or status retrieval, not a physical refresh.
Version is an operator-assigned uint32 label, not a signature or an anti-downgrade
counter. SHA-256 detects corruption; it does not authenticate the package author.

The tool privately consumes the ignored `secrets.env`; tokens never belong in
command arguments. All requests use the device's independent Bearer token, no
proxy and no redirects. `POST /api/push` retains the strict PNG/HTTP 200 physical
refresh contract. `GET /api/module` reports authenticated module status;
`POST /api/module` stages `application/octet-stream` and returns 202. Empty POSTs
to `?op=activate` and `?op=rollback` explicitly switch modules and return 200 only
when loading succeeds. These management responses do not indicate a panel
refresh. No public exposure, WebUI, SD or whole-firmware OTA is introduced.

## Confirmation and rollback

1. An upload is fully received and checked before flash writes. The inactive slot
   is erased; its old fallback marker is invalidated first. The confirmed active
   slot is never overwritten.
2. Payload is written and read back. The header is committed last; an incomplete
   write cannot become a valid candidate. Only then is `pending` recorded.
3. Activation persists `trial=1` before relocation. A load failure reopens the
   confirmed slot. A reset before confirmation clears the candidate and boots
   the confirmed slot, including panic/watchdog or power interruption.
4. A candidate becomes active only after the first valid PNG finishes physical
   refresh and final POWER_OFF wait. NVS commit establishes the old active as
   `previous`; input rejection does not confirm it.
5. Display I/O errors, BUSY timeouts, missing callbacks or entry failures during
   trial roll back. The failed request is not automatically rendered again.
6. Explicit rollback discards a staged/trial candidate, or switches to the previous
   confirmed slot. Uploading another candidate consumes that spare slot, while
   retaining the currently confirmed version.

The virgin A seed is the initial baseline. Do not deploy automatic pushes until
that baseline has passed a physical display test. A known-good fallback is only
as good as its actual display validation.

## Verification

```sh
python3 -m unittest discover -s tools -p 'test_*.py'
# Optional additional integration check against a real separately built ELF:
PHOTOFRAME_TEST_ELF=../photoframe/build-app/photoframe.app.elf \
  python3 -m unittest discover -s tools -p 'test_*.py'
idf.py -B build-independent build
```

Native tests compile the real firmware validation/state logic against a simulated
flash and NVS backend. A generated structural fixture makes the default suite
independent of sibling checkouts and build output; it is not executable firmware.
The tests require a C compiler and OpenSSL development headers/library. They cover truncated/corrupt ELF bounds, failed payload and
header writes, metadata commit failures, interrupted trials, confirmation and
manual rollback. The mock uses OpenSSL for actual SHA-256. Real hardware tests
must additionally prove module activation, a complete 200 refresh, confirmation,
reset rollback and subsequent service pushes. Do not equate successful build or
module activation with a successful physical display.
