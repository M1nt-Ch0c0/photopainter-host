# PhotoPainter BLE protocol v1

BLE belongs to the resident host. It is available without working Wi-Fi, valid SD
Wi-Fi JSON, or a running application. It never updates the host firmware or the
partition table. Applications are still trusted native ELF code, not a sandbox.

## Discovery

The peripheral advertises every 200–300 ms with flags `0x06` and the complete
128-bit service UUID `7b9d0001-6c82-4f31-9a48-6f5061696e74`. The scan response
contains manufacturer data (experimental company ID `0xffff`, little endian)
followed by this 20-byte record, and the short name `PPBLE`:

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 1 | Protocol version, 1 |
| 1 | 1 | 0 offline, 1 connecting, 2 IPv4 ready, 3 invalid Wi-Fi configuration |
| 2 | 6 | Stable device ID: factory Wi-Fi station MAC, displayed as 12 hex digits |
| 8 | 4 | IPv4 in network byte order; zero unless state is 2 |
| 12 | 8 | First 8 bytes of SHA-256 of SSID bytes; zero while offline/config-invalid |

The same record is readable through GATT. Wi-Fi connection attempts, disconnects
and IP changes update the record immediately and schedule an advertising update
on the NimBLE task. During a connection, the GATT record updates immediately;
advertising resumes with the latest record on disconnect. No SSID plaintext,
password, token, authentication proof, or remote service credential is advertised.
`0xffff` is a development identifier, not an assigned Bluetooth SIG company ID;
clients check this protocol's service/record and authenticate before trusting it.

SSID hashing avoids announcing its literal name. It does **not** hide common
SSID names from dictionary guessing. The stable ID also permits nearby tracking.
These are deliberate local discovery tradeoffs. The hash is only diagnostic;
it must not be used to identify or authenticate the device.

## GATT

One peripheral connection is supported; no central, observer, or standalone
broadcaster role is enabled. UUIDs below share suffix
`-6c82-4f31-9a48-6f5061696e74`.

| UUID prefix | Characteristic | Access |
|---|---|---|
| `7b9d0002` | Discovery | Read the 20-byte record; public |
| `7b9d0003` | Challenge | Read a fresh 32-byte device nonce for this connection |
| `7b9d0004` | Request | Write with response; bounded application framing below |
| `7b9d0005` | Response | Read a complete response, up to 512 bytes; ATT long reads supported |

Only discovery and authentication bootstrap are available before authentication.
The response characteristic returns a single zero byte while a request is pending.
This is not an authenticated acknowledgement or success. Poll with a delay
(the Go client uses 25 ms). Response bytes remain fixed until the next complete
request. No notification subscription is required for installation progress.

All multibyte integers below are little endian. Each GATT write has a 4-byte
prefix: `uint16 offset, uint16 total_frame_size`, then at least one frame byte.
The total frame is at most 512 bytes. Fragment size is at most `ATT_MTU - 3`;
even the minimum MTU 23 works, leaving 16 frame bytes per write. Identical
duplicate fragments are accepted. Holes, conflicting overlaps, invalid sizes,
and writes while a request is executing receive an ATT error. Only one request
can be outstanding; responses must be consumed before sending the next request.

## Authentication and threat model

There is no pairing, bonding, or link encryption. Authentication uses the existing
32–128 byte device push token as an HMAC-SHA256 key. The token never crosses BLE.
Use a randomly generated token: a captured exchange allows offline guessing of
a weak human-chosen token. The protocol provides authentication, integrity, and
replay protection, **not confidentiality** for packages or status information.
Passive observers can see them. Radio denial of service, disconnects and live
relaying are outside this protection. Native installed code is trusted.

Let `D` be the six device ID bytes, `S` the 32-byte random server nonce and `C`
an independently generated 32-byte client nonce. Concatenation is bytewise;
domain strings are ASCII without a trailing NUL:

1. Read discovery and check the selected ID. Read `S` from Challenge.
2. Send `01 || C || HMAC(token, "pp-ble-v1/client" || D || S || C)` (65 bytes).
3. Derive `K = HMAC(token, "pp-ble-v1/session" || D || S || C)` on both ends.
4. The host returns an ordinary signed response with op=1, seq=0, code=0 and
   the current discovery record. The client verifies it before trusting the IP.

The server nonce permits one authentication attempt per connection, including
malformed attempts. Reconnect for a new nonce after failure. Both directional
tags and the session key have different domains. Nonces come from ESP hardware
randomness after radio initialization and the computer's cryptographic RNG.

After authentication every command is:

`op:u8 || sequence:u32 || payload || HMAC(K, "pp-ble-v1/request" || preceding_bytes)`

Sequence starts at 1, must increase by exactly one, and cannot wrap. Every
authenticated response, including application errors, is:

`op:u8 || sequence:u32 || code:u16 || payload || HMAC(K, "pp-ble-v1/response" || preceding_bytes)`

Tags are full 32-byte SHA-256 HMACs and use constant-time equality checks.
Malformed/unauthenticated/expired requests may receive an unsigned 7-byte error.
Unsigned responses must never be accepted as success or evidence of identity.
A MAC/sequence/transport failure ends the client session. Do not retry an old
signed command on a fresh connection. Query state after an ambiguous activation.

Sessions expire after 60 seconds without a valid command, or two hours total.
The total cap accommodates a 1 MiB package at minimum MTU with acknowledged writes;
it is not a throughput guarantee. The computer's install deadline is 90 minutes.
An in-flight App Manager operation retains its data and finishes before cleanup;
the idle timer resumes after the operation. Disconnect discards authentication
and the partially received package. Reconnection supports **whole-package
retransmission**, not persisted cross-connection resume. This keeps the shared
installer unchanged and avoids extra Flash wear on partial transfers.

## Commands and errors

An application ID is 32 bytes: 1–31 lowercase ASCII letters, digits, `_` or `-`,
then NUL padding. Package bytes are exactly `tools/module.py package` output.

| Op | Command | Request payload | Successful response payload |
|---:|---|---|---|
| 1 | Authenticate | See handshake | Discovery record |
| 2 | Discovery, authenticated | Empty | Discovery record |
| 3 | Status / application list | `offset:u32` | `total:u32, offset:u32, JSON page` (up to 400 bytes) |
| 4 | Begin | `package_size:u32, full_package_sha256:32, app_id:32` | Received offset `u32` |
| 5 | Chunk | `offset:u32, data` | Received offset `u32` |
| 6 | Commit stage | Empty | Empty; explicitly activate next |
| 7 | Abort receive | Empty | Empty |
| 8 | Upload progress | Empty | `received:u32, total:u32` |
| 9 | Activate pending | `app_id:32` | Empty |
| 10 | Switch to confirmed active | `app_id:32` | Empty |
| 11 | Roll back | `app_id:32` | Empty |
| 12 | Remove | `app_id:32` | Empty |
| 13 | Application characteristic read (ABI 2.1) | `generation:u32, service_revision:u32, index:u32` | Value, at most 128 bytes |
| 14 | Application characteristic write (ABI 2.1) | Same 12-byte target, then value (0–128 bytes) | Empty, after application event completion |

Authenticated status includes live `uptime_ms` and the pinned IDF's numeric
`reset_reason`, sampled with heap counters. Useful reasons are 1 power-on,
3 software restart, 4 panic, 5 interrupt watchdog, 6 task watchdog, 9 brownout,
11 USB peripheral reset, 14 power glitch and 15 CPU lockup. These read-only fields
distinguish a radio disconnect from a device reboot without opening serial or
changing NVS. They are diagnostic metadata, not a new installation state.

Status offset zero freezes the existing App Manager JSON snapshot in this
session. Subsequent pages refer to that same snapshot. It contains the same
application catalog, selected/trial/loaded app, ready/confirmed flags and memory
fields as HTTP. Repeating Begin with identical metadata returns the current
offset. Duplicate package chunks must match the buffered bytes exactly.
Package size is 4148 through 1048576 bytes. The whole-package SHA is additional
transport protection; the existing installer also checks payload SHA, ELF,
manifest/imports, ABI and package identity before accepting a candidate.

| Code | Meaning |
|---:|---|
| 0 | Success |
| 1 | Unauthorized, invalid MAC/sequence, or invalid authentication attempt |
| 2 | Device token unconfigured/invalid |
| 3 | Shared admission busy (HTTP, button, timer/network event or another upload) |
| 4 | Invalid request, size, ID or unsupported opcode |
| 5 | Unexpected offset, gap or conflicting duplicate |
| 6 | Full-package SHA-256 mismatch |
| 7 | Memory unavailable |
| 8 | Invalid catalog/transfer state or missing application |
| 9 | Package rejected by the existing installer |
| 10 | Storage/application operation failure |
| 11 | Session expired |

Begin acquires the same admission semaphore as HTTP and the function key before
allocating up to 1 MiB in PSRAM. It holds admission through commit/abort/timeout/
disconnect. Commit calls **`app_manager_stage()`**, the identical path used by
HTTP, and releases input only after it returns. Explicit activate/switch/rollback/
remove use the same manager worker and catalog transitions. No BLE callback
loads ELF, writes a slot, invokes app code or touches the display. Long work never
blocks the NimBLE task. Connection generations prevent old completions being
published to a newly connected peer. The existing HTTP 401/409 and physical
refresh 200 meanings do not change. Successful staging never activates an app.

## Memory and build

`sdkconfig.defaults` enables NimBLE peripheral only, one connection, MTU 247,
12 blocks in each mbuf pool, external PSRAM host allocation, and Wi-Fi/BLE software
coexistence. Controller/DMA/task stacks retain the internal memory required by
ESP-IDF. PSRAM allocation failure is reported; it does not fall back to using a
megabyte of internal RAM. An upload and the 5 MiB HTTP PNG buffer cannot coexist
because they share admission. Link-layer security is deliberately disabled in
favor of the authenticated protocol above, not an unauthenticated fallback.

Use the pinned IDF commit `5e6f53cdb31fe5708eae3f55af9737be2822db22` and
Registry elf_loader **1.3.3**. Existing ignored `sdkconfig` files do not receive
new defaults automatically. Build into a fresh directory/config, for example:

```sh
idf.py -B build-ble -DSDKCONFIG=build-ble/sdkconfig build
idf.py -B build-ble size
python3 -m unittest discover -s tools -p 'test_*.py'
```

The partition table must remain byte-identical. After separate hardware
authorization and verified backups, a framework update writes **only** the
new host `.bin` at `0x10000`. Do not execute `idf.py flash` for this update.

Protocol vectors/tests: `tests/ble/test_protocol.c`, `tools/test_ble_protocol.py`,
and the computer repository's `internal/ble/protocol_test.go`. The C tests run
the production protocol with OpenSSL crypto and ASan/UBSan; firmware uses IDF
PSA HMAC/SHA. The catalog/runtime recovery tests remain authoritative for A/B.

Reference APIs: [ESP-IDF NimBLE](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/bluetooth/nimble/index.html),
[Go Bluetooth](https://github.com/tinygo-org/bluetooth), and
[Windows Runtime thread cleanup](https://learn.microsoft.com/en-us/windows/win32/api/roapi/nf-roapi-rouninitialize).

## Application BLE service (ABI 2.1)

The optional application surface is included in this implementation. It adds
three allowlisted imports: `app_host_ble_register_v2`, `app_host_ble_set_v2`,
`app_host_ble_unregister_v2`. `APP_MANIFEST_BLE` declares minor version 1.
The ordinary `APP_MANIFEST` macro still declares minor 0. Both validators reject
BLE imports in a minor-0 package and reject unknown minor versions. The original
context/result/manifest layouts remain 44/20/128 bytes on ELF32; each application's
context reports its declared minor version. ABI 1 and ABI 2.0 still work.

An active app may register one custom 128-bit service with up to four custom
characteristics. Each value is bounded to 128 bytes; properties are READ, WRITE,
and optional NOTIFY (which also requires READ). UUID arrays in the SDK are in
printed byte order. The host reserves the `7b9d` namespace and Bluetooth SIG
base UUID namespace, rejects duplicate UUIDs and copies the entire descriptor
and values into host RAM. No application pointer or callback is retained.

Register returns after validation/copy. Publication occurs on the NimBLE task,
including when the app START precedes BLE startup. `APP_BLE_ERROR` reports a
publication failure through the manager. Dynamic GATT services use NimBLE's
add/delete service API; built-in discovery/upload services are never removed.
The host reports Service Changed when the dynamic database changes. On STOP or
any unload/failure, app state and queued events are invalidated **before ELF
deallocation**; NimBLE removes the old service on its task. During that short
handoff, stale accesses already fail the revision check. RAM contains host copies
only, so neither old callbacks nor old BLE data can enter the next ELF.

`APP_EVENT_BLE` (6) carries a 140-byte `app_ble_event_v2_t` through the existing
borrowed context data pointer. Event types are authenticated CONNECTED,
DISCONNECTED, WRITTEN and ERROR. Anonymous connections produce no app events.
Connection boundaries coalesce undelivered events and discard the disconnected
peer's queued writes. At most eight events are buffered. The manager delivers
them only with shared admission held. A successful signed write waits for the
application's event result; a disconnect does not cancel an already executing
application event. The normal watchdog/display budgets still apply.

Read status to obtain `generation`, `ble_service_revision`,
`ble_characteristics` and `ble_service_uuid`. Ops 13/14 must name that generation,
revision and index; a switch or unregister/register invalidates the target.
Both commands require the same HMAC session and shared admission as management.
The computer CLI also checks the requested application ID matches `loaded_app`.

A custom characteristic's direct GATT WRITE accepts the identical fragmented
signed op-14 frame as Request; its index must match that characteristic. The
response still comes from the permanent Response characteristic. Direct GATT
READ requires an authenticated session and returns the current value. NOTIFY
also requires authentication and sends at most `MTU-3` bytes. These direct reads
and notifications are unencrypted, advisory values without protocol HMAC tags;
use **op 13**, as the provided CLI does, for an authenticated complete read.
Subscribe/read permissions never grant upload/activation permission on their own.

The minimal `photoframe/examples/ble-echo` app demonstrates START registration,
an authenticated write event, copied read/notify values and STOP cleanup. It has
no display requirement and does not draw on the panel.
