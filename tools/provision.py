#!/usr/bin/env python3
"""Generate and flash only the PhotoPainter NVS configuration partition."""

from __future__ import annotations

import argparse
import csv
import json
import struct
import re
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

NVS_OFFSET = 0x9000
NVS_SIZE = 0x6000
REQUIRED_IDF_COMMIT = "5e6f53cdb31fe5708eae3f55af9737be2822db22"
EXPECTED_KEYS = {"WIFI_SSID", "WIFI_PASSWORD", "PUSH_TOKEN"}
PARTITION_TABLE = Path(__file__).resolve().parent.parent / "partitions.csv"


def load_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue
        if "=" not in raw_line:
            raise ValueError(f"{path}:{number}: expected KEY=VALUE")
        key, value = raw_line.split("=", 1)
        key = key.strip()
        if key not in EXPECTED_KEYS:
            raise ValueError(f"{path}:{number}: unknown key {key!r}")
        if key in values:
            raise ValueError(f"{path}:{number}: duplicate key {key!r}")
        # Values are deliberately not trimmed: whitespace can be meaningful in
        # Wi-Fi credentials, and token validation must reject it rather than
        # silently provisioning a different credential.
        values[key] = value
    return values


def validate_network(ssid: str, password: str) -> None:
    if not isinstance(ssid, str) or not isinstance(password, str):
        raise ValueError("network fields must be strings")
    if not 1 <= len(ssid.encode()) <= 32 or any(ord(c) < 32 or ord(c) == 127 for c in ssid):
        raise ValueError("SSID must be 1..32 UTF-8 bytes without control characters")
    if password and not (8 <= len(password) <= 63 and all(32 <= ord(c) <= 126 for c in password)
                         or re.fullmatch(r"[0-9a-fA-F]{64}", password)):
        raise ValueError("password must be empty, 8..63 printable ASCII, or 64 hex digits")


def load_profiles(path: Path) -> list[tuple[str, str]]:
    def unique(pairs):
        d = {}
        for k, v in pairs:
            if k in d: raise ValueError("duplicate JSON key")
            d[k] = v
        return d
    if path.stat().st_size > 8192: raise ValueError("Wi-Fi JSON exceeds 8192 bytes")
    try:
        root = json.loads(path.read_text(), object_pairs_hook=unique)
    except (json.JSONDecodeError, RecursionError):
        raise ValueError("invalid Wi-Fi JSON") from None
    if not isinstance(root, dict) or type(root.get("version")) is not int or root["version"] != 1:
        raise ValueError("Wi-Fi JSON version must be 1")
    networks = root.get("networks")
    if not isinstance(networks, list) or len(networks) > 10:
        raise ValueError("expected at most 10 networks")
    profiles = []
    for n in networks:
        if not isinstance(n, dict): raise ValueError("invalid network entry")
        ssid, password = n.get("ssid"), n.get("password")
        validate_network(ssid, password)
        if any(previous[0] == ssid for previous in profiles): raise ValueError("duplicate SSID")
        profiles.append((ssid, password))
    return profiles


def profiles_blob(profiles: list[tuple[str, str]]) -> bytes:
    if len(profiles) > 10: raise ValueError("at most 10 networks")
    data = struct.pack("<II", 1, len(profiles))
    for ssid, password in profiles:
        validate_network(ssid, password)
        data += ssid.encode().ljust(33, b"\0") + password.encode().ljust(65, b"\0")
    return data.ljust(988, b"\0")


def validated(
    values: dict[str, str], *, allow_empty_push_token: bool = False
) -> tuple[str, str, str]:
    ssid = values.get("WIFI_SSID", "")
    password = values.get("WIFI_PASSWORD", "")
    token = values.get("PUSH_TOKEN", "")
    if not 1 <= len(ssid.encode()) <= 32:
        raise ValueError("WIFI_SSID must be 1..32 UTF-8 bytes")
    validate_network(ssid, password)
    try:
        token_bytes = token.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError(
            "PUSH_TOKEN must use printable ASCII without whitespace"
        ) from error
    token_length = len(token_bytes)
    if not (allow_empty_push_token and token_length == 0):
        if not 32 <= token_length <= 128:
            raise ValueError("PUSH_TOKEN must be 32..128 printable ASCII bytes")
        if any(byte < 0x21 or byte > 0x7e for byte in token_bytes):
            raise ValueError("PUSH_TOKEN must use printable ASCII without whitespace")
        if token == password:
            raise ValueError("PUSH_TOKEN must be independent from WIFI_PASSWORD")
        if token.startswith("replace-with-"):
            raise ValueError("replace the example PUSH_TOKEN before provisioning")
    return ssid, password, token


def require_pinned_idf(idf_path: Path) -> None:
    try:
        result = subprocess.run(
            ["git", "-C", str(idf_path), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"cannot verify pinned ESP-IDF: {error}") from error
    if result.stdout.strip() != REQUIRED_IDF_COMMIT:
        raise SystemExit(
            "IDF_PATH must point to ESP-IDF commit " + REQUIRED_IDF_COMMIT
        )


def require_nvs_layout() -> None:
    with PARTITION_TABLE.open(newline="", encoding="utf-8") as stream:
        rows = [
            row
            for row in csv.reader(stream)
            if row and not row[0].lstrip().startswith("#")
        ]
    matching = [row for row in rows if row[0].strip() == "nvs"]
    if len(matching) != 1 or len(matching[0]) < 5:
        raise SystemExit("partitions.csv must contain exactly one nvs partition")
    try:
        offset = int(matching[0][3].strip(), 0)
        size = int(matching[0][4].strip(), 0)
    except ValueError as error:
        raise SystemExit("nvs partition offset and size must be numeric") from error
    if (offset, size) != (NVS_OFFSET, NVS_SIZE):
        raise SystemExit(
            "provisioning layout does not match partitions.csv; refusing to flash"
        )


def copy_private(source: Path, destination: Path) -> None:
    destination = destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as output, source.open("rb") as input_file:
            shutil.copyfileobj(input_file, output)
    except BaseException:
        destination.unlink(missing_ok=True)
        raise


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port")
    parser.add_argument("--wifi-json", type=Path, help="ordered Wi-Fi profiles in the legacy SD JSON schema")
    parser.add_argument("--config", type=Path, default=Path("secrets.env"))
    parser.add_argument("--baud", default="460800")
    parser.add_argument("--generate-only", type=Path)
    parser.add_argument("--allow-empty-push-token", action="store_true")
    args = parser.parse_args()
    if args.generate_only is None and not args.port:
        parser.error("--port is required unless --generate-only is used")

    values = load_env(args.config)
    profiles = load_profiles(args.wifi_json) if args.wifi_json else None
    if profiles is not None:
        values["WIFI_SSID"], values["WIFI_PASSWORD"] = profiles[0] if profiles else ("unconfigured", "")
    ssid, password, token = validated(
        values,
        allow_empty_push_token=args.allow_empty_push_token,
    )
    if profiles is not None and any(password == token for _, password in profiles):
        raise ValueError("PUSH_TOKEN must be independent from all Wi-Fi passwords")
    idf_path_value = os.environ.get("IDF_PATH", "")
    if not idf_path_value:
        raise SystemExit("IDF_PATH is unset; source the pinned ESP-IDF export.sh")
    idf_path = Path(idf_path_value).resolve()
    require_pinned_idf(idf_path)
    require_nvs_layout()
    generator = (
        idf_path
        / "components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py"
    )
    if not generator.is_file():
        raise SystemExit(f"NVS generator not found: {generator}")

    with tempfile.TemporaryDirectory(prefix="photopainter-nvs-") as directory:
        work = Path(directory)
        csv_path = work / "config.csv"
        binary_path = work / "config.bin"
        with csv_path.open("w", newline="", encoding="utf-8") as stream:
            csv_path.chmod(0o600)
            writer = csv.writer(stream)
            writer.writerow(("key", "type", "encoding", "value"))
            writer.writerow(("photo", "namespace", "", ""))
            if profiles is None:
                writer.writerow(("wifi_ssid", "data", "string", ssid))
                writer.writerow(("wifi_pass", "data", "string", password))
            else:
                writer.writerow(("wifi_profiles", "data", "hex2bin", profiles_blob(profiles).hex()))
            writer.writerow(("push_token", "data", "string", token))

        subprocess.run(
            [sys.executable, str(generator), "generate", str(csv_path),
             str(binary_path), hex(NVS_SIZE)],
            check=True,
        )
        binary_path.chmod(0o600)
        if args.generate_only:
            copy_private(binary_path, args.generate_only)
            print(f"generated protected NVS image: {args.generate_only.resolve()}")
            return 0
        subprocess.run(
            [sys.executable, "-m", "esptool", "--chip", "esp32s3",
             "--port", args.port, "--baud", args.baud, "write-flash",
             hex(NVS_OFFSET), str(binary_path)],
            check=True,
        )
    print("NVS provisioned; secret values were not printed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as error:
        raise SystemExit(str(error)) from error
