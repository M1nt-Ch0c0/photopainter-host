#!/usr/bin/env python3
"""Package and update independent PhotoPainter ELF modules over authenticated LAN HTTP."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import re
from urllib.parse import urlencode
import urllib.request
import urllib.error

HEADER_SIZE = 4096
SLOT_SIZE = 1024 * 1024
MAGIC = 0x31464C45
ABI = 1
from app_manifest import manifest


def package_elf(data, version, app=None):
    if app is not None and not re.fullmatch(r"[a-z0-9_-]{1,31}", app):
        raise ValueError("app id must be 1..31 lowercase letters, digits, _ or -")
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
    metadata = manifest(data)
    abi = 2 if metadata else ABI
    if metadata:
        if app and app != metadata["id"]:
            raise ValueError("manifest and package app ID differ")
        app = metadata["id"]
    header = struct.pack(
        "<IIIII32s", MAGIC, 2 if app else 1, abi, len(data), version, hashlib.sha256(data).digest()
    )
    if app:
        header += app.encode().ljust(32, b"\0")
    return header + b"\xff" * (HEADER_SIZE - len(header)) + data


def token_from_config(path):
    # Consume credentials locally; never print values or pass in process arguments.
    from provision import load_env, validated

    return validated(load_env(path))[2]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def request(base, token, action, payload=None, app=None):
    if app is not None and not re.fullmatch(r"[a-z0-9_-]{1,31}", app):
        raise ValueError("invalid app ID")
    if action == "push":
        path = "/api/push"
        method = "POST"
        content_type = "image/png"
    else:
        path = "/api/module"
        method = "GET" if action == "status" else "POST"
        content_type = "application/octet-stream"
        query = {"app": app} if app else {}
        if action in ("activate", "rollback", "switch", "remove"):
            query["op"] = action
            payload = b""
        if query:
            path += "?" + urlencode(query)
    headers = {"Authorization": "Bearer " + token, "Content-Type": content_type}
    if action == "push" and app:
        headers["X-PhotoPainter-App"] = app
    req = urllib.request.Request(
        base.rstrip("/") + path,
        data=payload,
        method=method,
        headers=headers,
    )
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    try:
        with opener.open(req, timeout=660 if action in ("push", "switch", "activate", "rollback") else 60) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "action", choices=["package", "status", "stage", "activate", "rollback", "push", "switch", "remove"]
    )
    parser.add_argument("--app", help="independent application ID; legacy default: photoframe")
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
        data = package_elf(args.input.read_bytes(), args.version, args.app)
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
    code, body = request(args.url, token_from_config(args.config), args.action, payload, args.app)
    print(json.dumps({"status": code, "body": body}))
    return 0 if 200 <= code < 300 else 1


if __name__ == "__main__":
    raise SystemExit(main())
