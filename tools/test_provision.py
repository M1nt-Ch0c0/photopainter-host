import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

from provision import load_env, validated


class ProvisionValidationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.values = {
            "WIFI_SSID": "test-network",
            "WIFI_PASSWORD": "test-password",
            "PUSH_TOKEN": "t" * 32,
        }

    def test_regular_configuration_requires_push_token(self) -> None:
        self.values["PUSH_TOKEN"] = ""
        with self.assertRaisesRegex(ValueError, "32..128"):
            validated(self.values)

    def test_explicit_test_mode_allows_empty_push_token(self) -> None:
        self.values["PUSH_TOKEN"] = ""
        self.assertEqual(
            validated(self.values, allow_empty_push_token=True),
            ("test-network", "test-password", ""),
        )

    def test_test_mode_does_not_allow_a_short_nonempty_token(self) -> None:
        self.values["PUSH_TOKEN"] = "short"
        with self.assertRaisesRegex(ValueError, "32..128"):
            validated(self.values, allow_empty_push_token=True)

    def test_rejects_whitespace_or_non_ascii_push_token(self) -> None:
        for token in ("t" * 31 + " ", "t" * 31 + "\x7f", "t" * 31 + "中"):
            with self.subTest(token=token):
                self.values["PUSH_TOKEN"] = token
                with self.assertRaisesRegex(ValueError, "printable ASCII"):
                    validated(self.values)

    def test_load_env_preserves_values_and_token_whitespace_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / "config.env"
            config.write_text(
                "WIFI_SSID= network name \n"
                "WIFI_PASSWORD= password value \n"
                f"PUSH_TOKEN={'t' * 32} \n",
                encoding="utf-8",
            )
            loaded = load_env(config)
            self.assertEqual(loaded["WIFI_SSID"], " network name ")
            self.assertEqual(loaded["WIFI_PASSWORD"], " password value ")
            with self.assertRaisesRegex(ValueError, "printable ASCII"):
                validated(loaded)

    def test_empty_token_generate_only_image(self) -> None:
        idf_path = os.environ.get("IDF_PATH")
        if not idf_path:
            self.skipTest("IDF_PATH is not set")

        script = Path(__file__).with_name("provision.py")
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            config = temporary / "config.env"
            image = temporary / "config.nvs.bin"
            config.write_text(
                "WIFI_SSID=test-network\n"
                "WIFI_PASSWORD=test-password\n"
                "PUSH_TOKEN=\n",
                encoding="utf-8",
            )
            subprocess.run(
                [
                    sys.executable,
                    str(script),
                    "--config",
                    str(config),
                    "--generate-only",
                    str(image),
                    "--allow-empty-push-token",
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(image.stat().st_size, 0x6000)
            self.assertEqual(stat.S_IMODE(image.stat().st_mode), 0o600)


if __name__ == "__main__":
    unittest.main()
