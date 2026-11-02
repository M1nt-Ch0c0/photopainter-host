# Bootstrap a new development computer

Follow these steps in order. Do not copy build directories, managed components, NVS images or secret files from another checkout.

## 1. Install base tools

Install Git, CMake, Ninja, Python 3, a C compiler with the OpenSSL development library (the host tests need both), and a USB serial driver for the operating system. Install the Go version required by `ai-quota-frame/go.mod` (currently 1.26). Install Google Chrome or Chromium for frame rendering.

On Linux, give the user access to the serial device, commonly by joining `dialout`, then start a new login session. On Windows, use native PowerShell for COM-port work; WSL can build, but USB access may still need native Windows commands.

Detect the OS and package manager before installing anything, show the proposed commands, and change system packages only when authorized.

## 2. Clone the repositories

```sh
mkdir photopainter-workspace && cd photopainter-workspace
git clone https://github.com/M1nt-Ch0c0/photopainter-host.git
git clone https://github.com/M1nt-Ch0c0/photoframe.git
git clone https://github.com/M1nt-Ch0c0/ai-quota-frame.git
git clone https://github.com/M1nt-Ch0c0/photopainter-console.git
git clone https://github.com/M1nt-Ch0c0/esp32s3.git
```

Keep them as siblings; `tools/doctor.py` and the packaging examples assume that layout. Read each repository's `AGENTS.md` before changing it.

## 3. Install the pinned ESP-IDF

Use a dedicated checkout:

```sh
git clone https://github.com/espressif/esp-idf.git
git -C esp-idf checkout 5e6f53cdb31fe5708eae3f55af9737be2822db22
git -C esp-idf submodule update --init --recursive
cd esp-idf && ./install.sh esp32s3 && . ./export.sh
git rev-parse HEAD
```

The last command must print `5e6f53cdb31fe5708eae3f55af9737be2822db22`. On Windows, use the matching PowerShell export script.

## 4. Build and test photoframe

From `photoframe`:

```sh
cmake -S tests -B tests/build && cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
idf.py -B build-app -DIDF_TARGET=esp32s3 -DPHOTOFRAME_ARTIFACT=app build
```

The app artifact is `build-app/photoframe.app.elf`. See `photoframe/AGENTS.md` for the legacy `.so` build and the examples.

## 5. Build and test the host

From `photopainter-host` with the pinned IDF exported:

```sh
idf.py -B build-host -DSDKCONFIG=build-host/sdkconfig build
python3 -m unittest discover -s tools -p 'test_*.py'
python3 tools/doctor.py --strict
```

Package an app ELF with `python3 tools/module.py package --app ID --version N --input APP.elf --output APP.pkg`.

## 6. Set up the PC service

From `ai-quota-frame`:

```sh
cp .env.example .env
chmod 600 .env
make check
```

Fill in source credentials in `.env` without displaying or committing it. No device token is needed: pairing creates the client relationship. Set `CHROME_BIN` only if Chrome is not found automatically. Platform deployment (Linux `deploy/ai-quota-frame.service`, macOS `deploy/install-macos.py`, Windows) is described in that repository. Build the macOS GUI separately in `photopainter-console` with `sh build.sh`. Keep automatic pushing paused until the intended client is paired.

## 7. Pair and verify

1. Run `ai-quota-frame ble scan` to find the frame (`PP-XXXX`).
2. Run `ai-quota-frame ble pair --device-id 001122334455`, compare the six digits on the computer and the frame, and short-press the function key on the frame.
3. Run `ai-quota-frame ble status` and check the running app, `runtime_ready` and `confirmed`.

The browser plugin pairs on its own and becomes a separate owner. A frame that already has an owner needs the physical 8-second reset first.

## 8. Flash only with authorization

Before any device write, identify the exact device and port and make a verified 16 MiB Flash backup; image an installed microSD unless the user waives it. Use `ai-quota-frame/deploy/usb-maintenance.py`: `usb-flash` for a host update (factory only), `usb-provision` for NVS. The full procedure is in the [maintenance guide](https://github.com/M1nt-Ch0c0/esp32s3/blob/main/docs/08-maintenance.md).

Never open the serial port just to check liveness, and never hardcode a discovered port or IP in committed files.
