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
            subprocess.run([
                "cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-g", "-I" + str(ROOT / "main"),
                str(ROOT / "main/ble_protocol.c"),
                str(ROOT / "tests/ble/test_protocol.c"), "-lcrypto", "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)
