#!/usr/bin/env python3
"""Print a sanitized PhotoPainter development and device diagnostic report.

The doctor never reads .env, secrets.env, NVS images, or token values. Its HTTP
probe deliberately omits Authorization, so a configured device should answer
401 without touching the display. Serial probing is opt-in because opening the
ESP32-S3 native USB serial port can reset the device.
"""

from __future__ import annotations

import argparse
import base64
from dataclasses import asdict, dataclass
import glob
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
from typing import Iterable
from urllib import error, parse, request


REQUIRED_IDF_COMMIT = "5e6f53cdb31fe5708eae3f55af9737be2822db22"
REPOSITORIES = ("photopainter-host", "photoframe", "ai-quota-frame")
ROOT = Path(__file__).resolve().parent.parent
ESP_USB_ID = "303A:1001"


@dataclass(frozen=True)
class Check:
    name: str
    status: str
    detail: str


class Report:
    def __init__(self) -> None:
        self.checks: list[Check] = []

    def add(self, name: str, status: str, detail: object) -> None:
        self.checks.append(Check(name, status, str(detail)))

    def has_failures(self) -> bool:
        return any(check.status == "fail" for check in self.checks)

    def write(self, *, as_json: bool) -> None:
        if as_json:
            print(json.dumps([asdict(check) for check in self.checks], indent=2))
            return
        for check in self.checks:
            print(f"[{check.status.upper():4}] {check.name}: {check.detail}")


def run_output(arguments: list[str], *, timeout: float = 8) -> tuple[bool, str]:
    try:
        completed = subprocess.run(
            arguments,
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return False, str(exc)
    output = (completed.stdout or completed.stderr).strip()
    if not output and completed.returncode != 0:
        output = f"exit {completed.returncode}"
    return completed.returncode == 0, output


def add_tool(report: Report, name: str, arguments: list[str], *, required: bool) -> None:
    executable = shutil.which(arguments[0])
    if executable is None:
        report.add(name, "fail" if required else "warn", "not found")
        return
    ok, version = run_output([executable, *arguments[1:]])
    first_line = version.splitlines()[0] if version else "no version output"
    report.add(name, "pass" if ok else "fail", first_line)


def check_tools(report: Report) -> None:
    report.add("system", "info", f"{platform.system()} {platform.release()} ({platform.machine()})")
    report.add("python", "pass", platform.python_version())
    add_tool(report, "git", ["git", "--version"], required=True)
    add_tool(report, "cmake", ["cmake", "--version"], required=True)
    add_tool(report, "ninja", ["ninja", "--version"], required=True)
    add_tool(report, "go", ["go", "version"], required=True)

    chrome_candidates = [
        os.environ.get("CHROME_BIN", ""),
        shutil.which("google-chrome") or "",
        shutil.which("chromium") or "",
        shutil.which("chromium-browser") or "",
    ]
    if os.name == "nt":
        for variable in ("PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"):
            base = os.environ.get(variable)
            if base:
                chrome_candidates.append(
                    str(Path(base) / "Google/Chrome/Application/chrome.exe")
                )
    chrome = next((candidate for candidate in chrome_candidates if candidate and Path(candidate).is_file()), None)
    report.add("chrome", "pass" if chrome else "fail", chrome or "not found; set CHROME_BIN")


def check_idf(report: Report) -> None:
    raw_path = os.environ.get("IDF_PATH")
    if not raw_path:
        report.add("esp-idf", "fail", "IDF_PATH is unset")
        return
    idf_path = Path(raw_path).expanduser()
    if not idf_path.is_dir():
        report.add("esp-idf", "fail", f"IDF_PATH is not a directory: {idf_path}")
        return
    ok, commit = run_output(["git", "-C", str(idf_path), "rev-parse", "HEAD"])
    if not ok:
        report.add("esp-idf", "fail", f"cannot read revision: {commit}")
    elif commit != REQUIRED_IDF_COMMIT:
        report.add("esp-idf", "fail", f"revision {commit}; required {REQUIRED_IDF_COMMIT}")
    else:
        report.add("esp-idf", "pass", commit)

    idf_py = shutil.which("idf.py") or str(idf_path / "tools/idf.py")
    if not Path(idf_py).is_file():
        report.add("idf.py", "fail", "not found; export the pinned ESP-IDF environment")
        return
    ok, version = run_output([idf_py, "--version"], timeout=20)
    report.add("idf.py", "pass" if ok else "fail", version)


def check_repositories(report: Report, parent: Path) -> None:
    for name in REPOSITORIES:
        repository = parent / name
        if not (repository / ".git").exists():
            report.add(f"repo.{name}", "fail", f"missing at {repository}")
            continue
        ok, commit = run_output(["git", "-C", str(repository), "rev-parse", "HEAD"])
        if not ok:
            report.add(f"repo.{name}", "fail", commit)
            continue
        dirty_ok, dirty = run_output(
            ["git", "-C", str(repository), "status", "--porcelain"],
        )
        if not dirty_ok:
            report.add(f"repo.{name}", "fail", dirty)
            continue
        dirty_count = len(dirty.splitlines()) if dirty else 0
        status = "warn" if dirty_count else "pass"
        report.add(f"repo.{name}", status, f"{commit[:12]}, dirty-files={dirty_count}")


def is_wsl() -> bool:
    try:
        return "microsoft" in Path("/proc/version").read_text(encoding="utf-8").lower()
    except OSError:
        return False


def powershell_executable() -> str | None:
    discovered = shutil.which("powershell.exe") or shutil.which("powershell")
    if discovered:
        return discovered
    candidate = Path("/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe")
    return str(candidate) if candidate.is_file() else None


def windows_serial_ports() -> list[dict[str, str]]:
    powershell = powershell_executable()
    if not powershell:
        return []
    command = (
        "$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new();"
        "@(Get-CimInstance Win32_SerialPort | "
        "Select-Object DeviceID,Name,PNPDeviceID) | ConvertTo-Json -Compress"
    )
    try:
        completed = subprocess.run(
            [powershell, "-NoProfile", "-Command", command],
            check=False,
            capture_output=True,
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired):
        return []
    if completed.returncode != 0:
        return []
    try:
        decoded = completed.stdout.decode("utf-8-sig").strip()
        value = json.loads(decoded or "[]")
    except (UnicodeDecodeError, json.JSONDecodeError):
        return []
    if isinstance(value, dict):
        value = [value]
    ports: list[dict[str, str]] = []
    for entry in value if isinstance(value, list) else []:
        if not isinstance(entry, dict):
            continue
        ports.append(
            {
                "device": str(entry.get("DeviceID") or ""),
                "description": str(entry.get("Name") or ""),
                "hwid": str(entry.get("PNPDeviceID") or ""),
            }
        )
    return ports


def pyserial_ports() -> list[dict[str, str]]:
    try:
        from serial.tools import list_ports  # type: ignore[import-not-found]
    except ImportError:
        return []
    return [
        {
            "device": item.device,
            "description": item.description or "",
            "hwid": item.hwid or "",
        }
        for item in list_ports.comports()
    ]


def unix_serial_ports() -> list[dict[str, str]]:
    paths: set[str] = set()
    for pattern in ("/dev/serial/by-id/*", "/dev/ttyACM*", "/dev/ttyUSB*"):
        paths.update(glob.glob(pattern))
    return [
        {"device": path, "description": "serial device", "hwid": ""}
        for path in sorted(paths)
    ]


def unique_ports(groups: Iterable[list[dict[str, str]]]) -> list[dict[str, str]]:
    seen: set[str] = set()
    result: list[dict[str, str]] = []
    for group in groups:
        for entry in group:
            device = entry["device"]
            if not device or device in seen:
                continue
            seen.add(device)
            result.append(entry)
    return result


def find_serial_ports() -> list[dict[str, str]]:
    groups = [pyserial_ports()]
    if os.name == "nt" or is_wsl():
        groups.append(windows_serial_ports())
    if os.name != "nt":
        groups.append(unix_serial_ports())
    return unique_ports(groups)


def check_serial_devices(report: Report) -> list[dict[str, str]]:
    ports = find_serial_ports()
    if not ports:
        report.add("serial", "warn", "no serial ports found")
        return ports
    report.add("serial", "pass", f"{len(ports)} port(s) found")
    for entry in ports:
        marker = " ESP32-S3" if ESP_USB_ID in entry["hwid"].upper().replace("VID_", "").replace("&PID_", ":") else ""
        detail = f"{entry['description']} [{entry['hwid']}]".strip()
        report.add(f"serial.{entry['device']}", "info", detail + marker)
    return ports


def normalize_device_url(raw_url: str) -> str:
    parsed = parse.urlsplit(raw_url.strip())
    if parsed.scheme not in {"http", "https"} or not parsed.hostname:
        raise ValueError("device URL must be absolute HTTP(S)")
    if parsed.username or parsed.password:
        raise ValueError("device URL must not contain credentials")
    if parsed.path != "/api/push" or parsed.query or parsed.fragment:
        raise ValueError("device URL must end exactly in /api/push without query or fragment")
    return parse.urlunsplit(parsed)


class NoRedirect(request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):  # type: ignore[no-untyped-def]
        return None


def probe_http(raw_url: str, timeout: float) -> tuple[int | None, str]:
    url = normalize_device_url(raw_url)
    opener = request.build_opener(request.ProxyHandler({}), NoRedirect())
    probe = request.Request(
        url,
        data=b"",
        headers={"Content-Type": "image/png", "Connection": "close"},
        method="POST",
    )
    try:
        with opener.open(probe, timeout=timeout) as response:
            return response.status, response.read(256).decode("utf-8", "replace").strip()
    except error.HTTPError as exc:
        return exc.code, exc.read(256).decode("utf-8", "replace").strip()
    except (error.URLError, TimeoutError, OSError) as exc:
        return None, str(exc)


def check_http(report: Report, raw_url: str | None, timeout: float) -> None:
    if not raw_url:
        report.add(
            "device.http",
            "warn",
            "not probed; pass --device-url or set PHOTOFRAME_PUSH_URL",
        )
        return
    try:
        url = normalize_device_url(raw_url)
    except ValueError as exc:
        report.add("device.http", "fail", exc)
        return
    hostname = parse.urlsplit(url).hostname or "unknown"
    report.add("device.ip", "info", hostname)
    status, detail = probe_http(url, timeout)
    if status == 401:
        report.add("device.http", "pass", "401 configured and reachable")
    elif status == 503:
        report.add("device.http", "warn", f"503 reachable but unconfigured: {detail}")
    elif status is None:
        report.add("device.http", "fail", f"unreachable: {detail}")
    else:
        report.add("device.http", "fail", f"unexpected HTTP {status}: {detail}")


def extract_serial_facts(text: str) -> dict[str, str]:
    patterns = {
        "project": r"Project name:\s+([^\r\n]+)",
        "app-version": r"App version:\s+([^\r\n]+)",
        "esp-idf": r"ESP-IDF:\s+([^\r\n]+)",
        "elf-loader": r"ELF loader version:\s+([^\r\n]+)",
        "device-ip": r"wifi: IPv4:\s+([^\s]+)",
    }
    facts: dict[str, str] = {}
    for name, pattern in patterns.items():
        match = re.search(pattern, text)
        if match:
            facts[name] = match.group(1).strip()
    return facts


def choose_serial_port(requested: str | None, ports: list[dict[str, str]]) -> str | None:
    if requested:
        return requested
    esp_ports = [
        entry["device"]
        for entry in ports
        if "VID_303A&PID_1001" in entry["hwid"].upper()
        or ESP_USB_ID in entry["hwid"].upper()
    ]
    return esp_ports[0] if len(esp_ports) == 1 else None


def probe_serial(port: str, seconds: float) -> tuple[dict[str, str], str | None]:
    if re.fullmatch(r"COM\d+", port, re.IGNORECASE) and powershell_executable():
        return probe_windows_serial(port.upper(), seconds)
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        return {}, "pyserial is required for --probe-serial"
    try:
        device = serial.Serial(
            port=port,
            baudrate=115200,
            timeout=0.2,
            dsrdtr=False,
            rtscts=False,
        )
    except (OSError, serial.SerialException) as exc:
        return {}, str(exc)
    data = bytearray()
    try:
        import time

        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline and len(data) < 512 * 1024:
            chunk = device.read(device.in_waiting or 1)
            if chunk:
                data.extend(chunk)
    finally:
        device.close()
    facts = extract_serial_facts(data.decode("utf-8", "replace"))
    return facts, None


def probe_windows_serial(
    port: str, seconds: float
) -> tuple[dict[str, str], str | None]:
    powershell = powershell_executable()
    if not powershell or not re.fullmatch(r"COM\d+", port):
        return {}, "Windows PowerShell serial bridge is unavailable"
    command = (
        "$ErrorActionPreference='Stop';"
        "$OutputEncoding=[Console]::OutputEncoding=[Text.UTF8Encoding]::new();"
        f"$p=[System.IO.Ports.SerialPort]::new('{port}',115200,'None',8,'One');"
        "$p.DtrEnable=$false;$p.RtsEnable=$false;"
        "$b=[System.Text.StringBuilder]::new();"
        "try{$p.Open();"
        f"$until=[DateTime]::UtcNow.AddSeconds({seconds!r});"
        "while([DateTime]::UtcNow -lt $until){"
        "$s=$p.ReadExisting();if($s){[void]$b.Append($s)};"
        "Start-Sleep -Milliseconds 100}}"
        "finally{if($p.IsOpen){$p.Close()};$p.Dispose()};"
        "[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($b.ToString()))"
    )
    try:
        completed = subprocess.run(
            [powershell, "-NoProfile", "-Command", command],
            check=False,
            capture_output=True,
            timeout=seconds + 10,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {}, str(exc)
    if completed.returncode != 0:
        message = completed.stderr.decode("utf-8", "replace").strip()
        return {}, message or f"PowerShell exited {completed.returncode}"
    try:
        encoded = completed.stdout.decode("utf-8-sig").strip().splitlines()[-1]
        text = base64.b64decode(encoded, validate=True).decode("utf-8", "replace")
    except (IndexError, UnicodeDecodeError, ValueError) as exc:
        return {}, f"cannot decode PowerShell serial output: {exc}"
    return extract_serial_facts(text), None


def check_serial_firmware(
    report: Report,
    *,
    enabled: bool,
    requested_port: str | None,
    ports: list[dict[str, str]],
    seconds: float,
) -> None:
    if not enabled:
        report.add(
            "firmware.app-version",
            "info",
            "not probed; --probe-serial may reset the device",
        )
        return
    port = choose_serial_port(requested_port, ports)
    if not port:
        report.add(
            "firmware.app-version",
            "fail",
            "select one port with --serial; serial probing resets the device",
        )
        return
    report.add("firmware.serial-warning", "warn", f"opening {port} may reset the device")
    facts, failure = probe_serial(port, seconds)
    if failure:
        report.add("firmware.app-version", "fail", failure)
        return
    if not facts:
        report.add("firmware.app-version", "warn", "no recognizable boot log captured")
        return
    for name, value in facts.items():
        report.add(f"firmware.{name}", "pass" if name != "device-ip" else "info", value)
    if "app-version" not in facts:
        report.add("firmware.app-version", "warn", "not present in captured serial output")


def check_secret_presence(report: Report) -> None:
    for name in (
        "CLIPROXY_MANAGEMENT_KEY",
        "PHOTOFRAME_PUSH_TOKEN",
        "CPAMP_ADMIN_KEY",
    ):
        report.add(f"env.{name}", "info", "set (redacted)" if name in os.environ else "unset")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-parent",
        type=Path,
        default=ROOT.parent,
        help="directory containing the three sibling repositories",
    )
    parser.add_argument(
        "--device-url",
        help="complete device URL, for example http://192.168.1.50/api/push",
    )
    parser.add_argument("--timeout", type=float, default=5, help="HTTP probe timeout in seconds")
    parser.add_argument("--serial", help="serial device or COM port to probe")
    parser.add_argument(
        "--probe-serial",
        action="store_true",
        help="open serial and capture boot facts; this may reset the device",
    )
    parser.add_argument(
        "--serial-seconds",
        type=float,
        default=8,
        help="seconds to capture after opening serial",
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit nonzero when any required check fails",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")
    if args.serial_seconds <= 0 or args.serial_seconds > 60:
        raise SystemExit("--serial-seconds must be in (0, 60]")

    report = Report()
    check_tools(report)
    check_idf(report)
    check_repositories(report, args.repo_parent.resolve())
    check_secret_presence(report)
    ports = check_serial_devices(report)
    device_url = args.device_url or os.environ.get("PHOTOFRAME_PUSH_URL")
    check_http(report, device_url, args.timeout)
    check_serial_firmware(
        report,
        enabled=args.probe_serial,
        requested_port=args.serial,
        ports=ports,
        seconds=args.serial_seconds,
    )
    report.write(as_json=args.json)
    return 1 if args.strict and report.has_failures() else 0


if __name__ == "__main__":
    raise SystemExit(main())
