#!/usr/bin/env python3
"""Package and update independent PhotoPainter ELF modules over authenticated LAN HTTP."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import urllib.request
import urllib.error

HEADER_SIZE = 4096
SLOT_SIZE = 1024 * 1024
MAGIC = 0x31464C45
ABI = 1


def package_elf(data, version):
    if (
        len(data) < 52
        or len(data) > SLOT_SIZE - HEADER_SIZE
        or data[:7] != b"\x7fELF\x01\x01\x01"
    ):
        raise ValueError("expected bounded ELF32 little endian payload")
    if struct.unpack_from("<HH", data, 16) != (3, 94):
        raise ValueError("expected Xtensa ET_DYN app ELF")
    if not 0 <= version <= 0xFFFFFFFF:
        raise ValueError("version must fit uint32")
    header = struct.pack(
        "<IIIII32s", MAGIC, 1, ABI, len(data), version, hashlib.sha256(data).digest()
    )
    return header + b"\xff" * (HEADER_SIZE - len(header)) + data


def token_from_config(path):
    # Consume credentials locally; never print values or pass in process arguments.
    from provision import load_env, validated

    return validated(load_env(path))[2]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def request(base, token, action, payload=None):
    if action == "push":
        path = "/api/push"
        method = "POST"
        content_type = "image/png"
    else:
        path = "/api/module"
        method = "GET" if action == "status" else "POST"
        content_type = "application/octet-stream"
        if action in ("activate", "rollback"):
            path += "?op=" + action
            payload = b""
    req = urllib.request.Request(
        base.rstrip("/") + path,
        data=payload,
        method=method,
        headers={"Authorization": "Bearer " + token, "Content-Type": content_type},
    )
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    try:
        with opener.open(req, timeout=660 if action == "push" else 60) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action", choices=["package", "status", "stage", "activate", "rollback", "push"]
    )
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--version", type=int, default=1)
    parser.add_argument("--url")
    parser.add_argument(
        "--config",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "secrets.env",
    )
    args = parser.parse_args()
    if args.action == "package":
        if not args.input or not args.output:
            parser.error("package requires --input and --output")
        data = package_elf(args.input.read_bytes(), args.version)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(data)
        print(
            json.dumps(
                {
                    "file": str(args.output),
                    "bytes": len(data),
                    "version": args.version,
                    "sha256": hashlib.sha256(data).hexdigest(),
                }
            )
        )
        return 0
    if not args.url:
        parser.error("--url is required")
    if args.action in ("push", "stage") and not args.input:
        parser.error("--input is required")
    payload = args.input.read_bytes() if args.input else None
    code, body = request(args.url, token_from_config(args.config), args.action, payload)
    print(json.dumps({"status": code, "body": body}))
    return 0 if 200 <= code < 300 else 1


if __name__ == "__main__":
    raise SystemExit(main())
