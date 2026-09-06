# Bootstrap a new development computer

Follow this order. Do not copy build directories, managed components, NVS images, or secret files from another checkout.

## 1. Install base tools

Install Git with submodule support, CMake, Ninja, Python 3, and a USB serial driver supported by the operating system. Install Go 1.26 or the version required by `ai-quota-frame/go.mod`. Install Google Chrome or Chromium for frame rendering.

On Linux, ensure the user can access the serial device, commonly by joining `dialout`, then start a new login session. On Windows, use a native PowerShell terminal for COM-port and hotspot diagnostics. WSL can build, but USB and Windows Mobile Hotspot state may still need native Windows commands.

Do not assume a package-manager command in automation. Detect the OS and package manager, show the proposed installation, and avoid changing system packages unless authorized.

## 2. Clone sibling repositories

```bash
mkdir photopainter-workspace
cd photopainter-workspace
git clone https://github.com/M1nt-Ch0c0/photopainter-host.git
git clone https://github.com/M1nt-Ch0c0/photoframe.git
git clone https://github.com/M1nt-Ch0c0/ai-quota-frame.git
```

Read each repository's `AGENTS.md` before making changes. Keep the sibling layout for convenience; the host no longer depends on `PHOTOFRAME_COMPONENT_DIR`.

## 3. Install the pinned ESP-IDF

Use a dedicated IDF checkout rather than whichever IDF happens to be installed globally:

```bash
git clone https://github.com/espressif/esp-idf.git
git -C esp-idf checkout 5e6f53cdb31fe5708eae3f55af9737be2822db22
git -C esp-idf submodule update --init --recursive
cd esp-idf
./install.sh esp32s3
. ./export.sh
git rev-parse HEAD
```

The final command must print exactly `5e6f53cdb31fe5708eae3f55af9737be2822db22`. On Windows, run the corresponding ESP-IDF PowerShell export script. Do not patch the Registry copy of `elf_loader`.

## 4. Validate the component

From `photoframe`:

```bash
cmake -S tests -B tests/build
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure

idf.py -B build-app -DIDF_TARGET=esp32s3 \
  -DPHOTOFRAME_ARTIFACT=app build
idf.py -B build-so -DIDF_TARGET=esp32s3 \
  -DPHOTOFRAME_ARTIFACT=so build
```

Expected artifacts are `build-app/photoframe.app.elf` and `build-so/photoframe.so`.

## 5. Build the host

From `photopainter-host` with the pinned IDF exported:

```bash
idf.py set-target esp32s3
idf.py build
python3 tools/doctor.py --strict
```

The host build is independent. Package the separately built app ELF with `tools/module.py`. Follow [module slots](../../../../docs-module-slots.md) for first migration and later business updates; subsequent updates use the inactive slot without rebuilding the host.

Do not flash yet. First identify the exact device and make a verified 16 MiB Flash backup. If a microSD is installed, power off, remove it, and image it with a reader unless the user explicitly waives the SD backup for that run.

## 6. Configure the PC service

From `ai-quota-frame`:

```bash
cp .env.example .env
chmod 600 .env
make check
```

Set `CHROME_BIN` only when automatic Chrome discovery fails. Populate `.env` locally with independently generated values. Never display or commit `.env`, and never reuse a management key as `PHOTOFRAME_PUSH_TOKEN`.

Linux service deployment uses `deploy/ai-quota-frame.service`. For interactive Windows use, load `.env` into the process environment without printing values and run the native Windows build. For macOS, use `deploy/install-source-tunnel.py` and `deploy/install-macos.py` as described in the service README; keep the pusher disabled until a successful one-shot physical test. There is no inbound host HTTP port.

## 7. Discover before operating hardware

Run the doctor from `photopainter-host`:

```bash
python3 tools/doctor.py --device-url http://DEVICE_IP/api/push
```

The unauthenticated probe does not change the screen. A configured healthy device returns 401. Use the explicit serial probe only when a reset is acceptable:

```bash
python3 tools/doctor.py \
  --device-url http://DEVICE_IP/api/push \
  --serial SERIAL_PORT \
  --probe-serial
```

Do not hardcode the discovered IP or port into committed files. DHCP and COM assignments can change.

## 8. Flash, provision, and push only with authorization

After backup and target verification, the host README contains the supported flash and NVS provisioning commands. After provisioning, first issue an unauthenticated probe and expect 401. Then make a single authenticated test push and wait for HTTP 200. Visually confirm content, orientation, and all six colors.
