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

    def test_v2_configuration_does_not_require_or_accept_a_fixed_token(self) -> None:
        for token in (None, "", "short", "replace-with-old-token", "旧值"):
            values = dict(self.values)
            if token is None:
                values.pop("PUSH_TOKEN")
            else:
                values["PUSH_TOKEN"] = token
            self.assertEqual(validated(values), ("test-network", "test-password", ""))

    def test_explicit_legacy_configuration_requires_push_token(self) -> None:
        self.values["PUSH_TOKEN"] = ""
        with self.assertRaisesRegex(ValueError, "32..128"):
            validated(self.values, legacy_token=True)

    def test_explicit_test_mode_allows_empty_push_token(self) -> None:
        self.values["PUSH_TOKEN"] = ""
        self.assertEqual(
            validated(self.values, legacy_token=True, allow_empty_push_token=True),
            ("test-network", "test-password", ""),
        )

    def test_test_mode_does_not_allow_a_short_nonempty_token(self) -> None:
        self.values["PUSH_TOKEN"] = "short"
        with self.assertRaisesRegex(ValueError, "32..128"):
            validated(self.values, legacy_token=True, allow_empty_push_token=True)

    def test_rejects_whitespace_or_non_ascii_push_token(self) -> None:
        for token in ("t" * 31 + " ", "t" * 31 + "\x7f", "t" * 31 + "中"):
            with self.subTest(token=token):
                self.values["PUSH_TOKEN"] = token
                with self.assertRaisesRegex(ValueError, "printable ASCII"):
                    validated(self.values, legacy_token=True)

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
                validated(loaded, legacy_token=True)

    def test_v2_generate_only_image_omits_old_token_and_legacy_is_explicit(self) -> None:
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
                "PUSH_TOKEN=" + "t" * 32 + "\n",
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
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertEqual(image.stat().st_size, 0x6000)
            self.assertEqual(stat.S_IMODE(image.stat().st_mode), 0o600)
            self.assertNotIn(b"push_token", image.read_bytes())
            self.assertNotIn(b"t" * 32, image.read_bytes())
            legacy = temporary / "legacy.nvs.bin"
            subprocess.run(
                [sys.executable, str(script), "--config", str(config),
                 "--legacy-token", "--generate-only", str(legacy)],
                check=True, capture_output=True, text=True,
            )
            self.assertEqual(legacy.stat().st_size, 0x6000)
            self.assertEqual(stat.S_IMODE(legacy.stat().st_mode), 0o600)
            self.assertIn(b"push_token", legacy.read_bytes())


if __name__ == "__main__":
    unittest.main()
