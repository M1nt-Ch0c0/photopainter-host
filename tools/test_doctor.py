from __future__ import annotations

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys
import threading
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parent))
import doctor  # noqa: E402


class DoctorTest(unittest.TestCase):
    def test_normalize_device_url(self) -> None:
        self.assertEqual(
            doctor.normalize_device_url("http://192.0.2.1/api/push"),
            "http://192.0.2.1/api/push",
        )
        for invalid in (
            "192.0.2.1/api/push",
            "ftp://192.0.2.1/api/push",
            "http://user:password@192.0.2.1/api/push",
            "http://192.0.2.1/",
            "http://192.0.2.1/api/push?token=secret",
        ):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    doctor.normalize_device_url(invalid)

    def test_extract_serial_facts(self) -> None:
        facts = doctor.extract_serial_facts(
            """
I app_init: Project name:     photopainter_host
I app_init: App version:      abc1234
I app_init: ESP-IDF:          v6.0.3-1-g5e6f53cd
I ELF: ELF loader version: 1.3.3
I wifi: IPv4: 192.0.2.44
"""
        )
        self.assertEqual(
            facts,
            {
                "project": "photopainter_host",
                "app-version": "abc1234",
                "esp-idf": "v6.0.3-1-g5e6f53cd",
                "elf-loader": "1.3.3",
                "device-ip": "192.0.2.44",
            },
        )

    def test_unauthenticated_probe_returns_401_without_authorization(self) -> None:
        observed: dict[str, object] = {}

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self) -> None:
                observed["path"] = self.path
                observed["authorization"] = self.headers.get("Authorization")
                observed["content_length"] = self.headers.get("Content-Length")
                self.send_response(401)
                self.end_headers()
                self.wfile.write(b"unauthorized\n")

            def log_message(self, *args: object) -> None:
                pass

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        try:
            status, body = doctor.probe_http(
                f"http://127.0.0.1:{server.server_port}/api/push",
                2,
            )
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

        self.assertEqual(status, 401)
        self.assertEqual(body, "unauthorized")
        self.assertEqual(observed["path"], "/api/push")
        self.assertIsNone(observed["authorization"])
        self.assertEqual(observed["content_length"], "0")

    def test_report_strict_failure_detection(self) -> None:
        report = doctor.Report()
        report.add("optional", "warn", "missing")
        self.assertFalse(report.has_failures())
        report.add("required", "fail", "missing")
        self.assertTrue(report.has_failures())

    def test_choose_unique_esp_serial_port(self) -> None:
        ports = [
            {"device": "COM3", "description": "other", "hwid": "PCI"},
            {
                "device": "COM55",
                "description": "USB serial device",
                "hwid": r"USB\VID_303A&PID_1001&MI_00",
            },
        ]
        self.assertEqual(doctor.choose_serial_port(None, ports), "COM55")
        self.assertEqual(doctor.choose_serial_port("COM7", ports), "COM7")


if __name__ == "__main__":
    unittest.main()
