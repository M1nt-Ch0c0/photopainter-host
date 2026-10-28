# PhotoPainter physical pairing v2

Protocol contract frozen 2026-10-29. The host owns pairing, its system display,
physical button and NVS. No application receives a pairing key. Existing image,
APP management and CoC framing remains unchanged after BondAuth.

"No application receives a key" describes the SDK boundary. Installed APPs
remain trusted native ELF code in shared address space, not a process sandbox.
Physical reset revokes client relationships; it is not a factory wipe or a safe
transfer guarantee in the presence of an already-installed malicious APP.

**尚未实机验证 / not hardware-validated.** The implementation has offline state-
machine, storage, concurrency and cross-language vector coverage. Physical
button approval, reboot persistence, reset/re-pair and actual image completion
must be recorded on the target device before claiming end-to-end acceptance.

## Identity, framing and encodings

The 20-byte discovery record is unchanged except byte 0 is **2**. The permanent
service/characteristic UUIDs and `offset:u16le,total:u16le,fragment` writes remain
unchanged (logical frame <=512 bytes). The advertisement name is `PP-XXXX` (the last four stable-ID hexadecimal
digits), placed in advertising data; the manufacturer scan record is unchanged.
The 32-byte Challenge is fresh for every
GAP connection. Its first 16 bytes are the proposed binding ID for a new pairing.
A physical reset disconnects the peer and thus forces a new proposal.

All integers below are unsigned little endian. `D` is the six stable device-ID
bytes. `CID` is a random, nonzero 16-byte client identity. `BID` is a nonzero
16-byte binding identity proposed by the device's Challenge (not supplied
arbitrarily by a new owner). Public keys are P-256 uncompressed SEC1, 65 bytes,
starting with 0x04; private keys are 32 bytes. Nonces are 32 bytes. Client name
`N` is 1..32 printable ASCII bytes (0x20..0x7e), with no leading/trailing spaces;
its one-byte length is `L`. Names are display hints, not authenticated identity
until the full transcript and physical confirmation succeed.

Every reply starts `op:u8 | sequence:u32le=0 | code:u16le`. Codes use the existing
BLE enum: 0 OK, 1 unauthorized, 2 no binding, 3 busy, 4 invalid, 8 state conflict,
10 persistence error, 11 expired, 13 physical display error. A reply that should
be signed is invalid unless its exact length, echoed IDs/challenge and MAC match.
Unsigned discovery/errors never establish identity or successful pairing.

States: 0 unbound, 1 waiting_reveal, 2 waiting_client_ready, 3 displaying,
4 awaiting_button, 5 bound, 6 cancelled, 7 expired, 8 fault. Unbound/fault states
never grant management access. A corrupt NVS binding fails closed until physical
reset; it never falls back to the old token.

## Bootstrap operations

| op | request after op byte | success payload after 7-byte header | signature |
|---|---|---|---|
| 18 PairInfo | empty | `2:u8,state:u8,BID:16,CID:16,remaining_ms:u32,L:u8,N:L` | none |
| 19 PairCommit | `CID:16,BID:16,Ccommit:32,L:u8,N:L` | `2:u8,BID:16,Spub:65,Scommit:32` | none |
| 20 PairReveal | `BID:16,Cpub:65,Cnonce:32` | `2:u8,BID:16,Snonce:32,state:u8,remaining_ms:u32,Challenge:32` | root / response |
| 21 PairPoll | `BID:16,proof:32` | `2:u8,BID:16,state:u8,remaining_ms:u32,Challenge:32` | root / response |
| 22 PairCancel | `BID:16,proof:32` | same as PairPoll | root / response |
| 23 BondAuth | `CID:16,BID:16,Cnonce:32,proof:32` | original 20-byte discovery record | session / existing response |

PairInfo reports the current binding/candidate IDs and name. When there is no
candidate or binding, BID is Challenge[0:16], CID/name are zero/empty. PairCommit
is admitted only while unbound, and a new BID must equal Challenge[0:16]. Exact
repeats of an in-progress CID/BID/commit/name return the original server public
key/commitment without regenerating secrets. A different active candidate is BUSY. After cancellation/expiry and cooldown, a
new pairing is allowed only with a fresh BID from a new connection; a terminal
BID cannot be reused. A bound device returns STATE and requires a physical reset for a new owner.

## Commitment, key derivation and comparison

Concatenation uses raw bytes and ASCII domain strings with **no NUL terminator**.
The protocol byte `V` is one byte 0x02. Roles are separated by these domains:

```
Ccommit = SHA256("pp-pair-v2/client-commit" || V || D || CID || BID || Cpub || Cnonce || L || N)
Scommit = SHA256("pp-pair-v2/server-commit" || V || D || CID || BID || Ccommit || Spub || Snonce || L || N)
T = V || D || CID || BID || Ccommit || Scommit || Cpub || Cnonce || Spub || Snonce || L || N
Z = P256-ECDH(Cprivate, Spub) = P256-ECDH(Sprivate, Cpub)  # 32-byte x coordinate
salt = SHA256("pp-pair-v2/salt" || T)
root = HKDF-SHA256(ikm=Z, salt=salt, info="pp-pair-v2/bond", length=32)
code = LE32(HMAC-SHA256(root, "pp-pair-v2/code" || T)[0:4]) mod 1000000
```

Display `code` as exactly six decimal digits, including leading zeros. The
server never sends the code in a public characteristic. The client commits its
key/nonce before learning the server nonce; the server commits its key/nonce
before the client reveals. Both commitments must be checked exactly.

The PairReveal / PairPoll / PairCancel reply MAC is appended as 32 bytes:
`HMAC-SHA256(root, "pp-pair-v2/response" || entire_header_and_payload)`.

Poll/Cancel proof is:
`HMAC-SHA256(root, domain || V || D || CID || BID || current_Challenge)` where
`domain` is respectively `pp-pair-v2/poll` or `pp-pair-v2/cancel`. Binding each
proof/reply to the current Challenge prevents cross-connection reply replay.

The client durably stores the candidate root and identity before its first
PairPoll. This first valid root proof arms the device prompt. PairReveal alone
never displays an actionable confirmation and cannot be physically approved.

## Physical confirmation and lifetime

The host serializes a pairing screen through its sole App Manager/display
admission. It shows the client name and six digits. Only after the complete
physical refresh plus POWER_OFF succeeds does state become awaiting_button.
The same short button gesture must begin and end during that exact candidate's
awaiting_button state. A press during rendering/another operation never counts.

A valid short press commits the single binding NVS blob (CID, BID, root, name)
before state becomes bound. Failed persistence does not authenticate the client.
Physical reset writes a valid versioned unbound tombstone instead of erasing
the owner key. Only a namespace that has never existed is factory-unbound;
an existing namespace with a missing/corrupt owner is a storage fault, including
the NVS lower-layer CRC failure that erases a blob then returns NOT_FOUND.
After acceptance the host shows a bound/ready system screen; unrelated APPs are
not switched or updated. The next normal app picture replaces this screen.

A release after holding the button for at least 8 seconds latches a physical
reset request, including while an image is refreshing. It immediately revokes
GATT/CoC authorization and blocks new work/displays. The already-started physical
refresh completes through POWER_OFF; then the binding and pending candidate are
durably cleared and a ready-to-pair screen is shown. Reset does not erase Wi-Fi,
installed APPs or the last picture cache. No remote operation can clear or
replace a bound owner.

Neither an upload's held admission nor back-to-back physical refreshes can
prevent this gesture. Display begin and reset request use one atomic control
gate: an existing refresh continues, but no later refresh can begin after the
request. The transport owner thread finishes/cancels its
current local request, closes its session and releases upload buffers/admission,
then acknowledges the exact reset serial. The manager cancels old queued jobs
and commits the tombstone only after this acknowledgment. Every BLE job also
carries the captured authorization epoch, checked again before its side effect.
Bootstrap frames use the connection-open epoch, so old public challenges cannot
recreate a candidate after reset. Existing running manager work is bounded and
finishes before the reset commit; queued old-owner work does not run afterward.

Waiting-reveal and waiting-client-ready deadlines are 30 seconds each. Prompt
rendering has the existing 650-second display budget; confirmation lasts 120
seconds after the prompt physically completes. One candidate at a time; at
least 60 seconds between distinct starts. Cancellation and expiry never create
an owner. PairCancel requires the candidate root proof and is refused after
binding. If a connection drops, the candidate may be resumed by its exact BID
and root proof within its lifetime. Reboot loses an unapproved RAM candidate.
A persisted approved owner can prove it after reconnecting even if the original
approval reply was lost.

## BondAuth and existing operations

The client uses its saved CID/BID/root. The current GATT Challenge is `S`:

```
A = V || D || CID || BID || S || Cnonce
proof = HMAC-SHA256(root, "pp-bond-v2/client" || A)
session = HMAC-SHA256(root, "pp-bond-v2/session" || A)
```

One authentication attempt per fresh Challenge, including malformed attempts.
BondAuth success is op 23, sequence 0, code 0, discovery record, then
`HMAC-SHA256(session, "pp-ble-v1/response" || header_and_record)`.
Normal signed requests/replies (ops 2..17), sequence rules, APP target pinning,
PFI1 receipts and CoC key derivation use the existing contracts with this new
session key. Pairing does not report an image refresh as complete.

Old op 1 fixed-token authentication is permanently rejected by v2 firmware.
Legacy HTTP token-authenticated mutation/management routes return 410 and never
execute, so an old NVS token cannot bypass physical reset. BLE management remains
available to the paired owner. Credentials are never emitted into logs/status.
Clients must pin BID alongside device ID for pending image recovery; after a
binding change, they must not recover or redraw an earlier owner's transaction.

Pairing establishes authenticated ownership and fresh signed sessions. Existing
GATT/CoC image payloads retain their previous wire format: authentication and
integrity are provided, but this protocol does not claim radio confidentiality.
