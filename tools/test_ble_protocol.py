"""Run production C protocol with real host SHA/HMAC and deterministic I/O."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BLEProtocolTests(unittest.TestCase):
    def test_auth_transfer_replay_lifetime_and_framing(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "ble-protocol"
            flags=[]
            ssl=Path("/opt/homebrew/opt/openssl@3")
            if ssl.exists(): flags=["-I"+str(ssl/"include"),"-L"+str(ssl/"lib")]
            subprocess.run([
                "cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-g", "-I" + str(ROOT / "main"),
                str(ROOT / "main/ble_protocol.c"),
                str(ROOT / "main/ble_shell.c"),
                str(ROOT / "tests/ble/test_protocol.c"), "-lcrypto", "-o", str(binary),
            ]+flags, check=True)
            subprocess.run([str(binary)], check=True)

    def test_shell_paging_and_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "ble-shell"
            flags=[]
            ssl=Path("/opt/homebrew/opt/openssl@3")
            if ssl.exists(): flags=["-I"+str(ssl/"include"),"-L"+str(ssl/"lib")]
            subprocess.run([
                "cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-g", "-I" + str(ROOT / "main"),
                str(ROOT / "main/ble_protocol.c"), str(ROOT / "main/ble_shell.c"),
                str(ROOT / "tests/ble/test_shell.c"), "-lcrypto", "-o", str(binary),
            ]+flags, check=True)
            subprocess.run([str(binary)], check=True)
