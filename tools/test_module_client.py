import json
import struct
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
import unittest
from module import package_elf, request
from module_test_fixture import minimal_elf


class ModuleClientTests(unittest.TestCase):
    def test_format2_binds_id_and_preserves_legacy_abi_layout(self):
        package = package_elf(minimal_elf(), 27, "clock")
        self.assertEqual(struct.unpack_from("<IIIII", package), (0x31464C45, 2, 1, len(minimal_elf()), 27))
        self.assertEqual(package[52:84], b"clock" + b"\0"*27)
        self.assertEqual(package[4096:], minimal_elf())
        for name in ("", "../clock", "UPPER", "x"*32, "a&op=remove", "中文"):
            with self.assertRaises(ValueError): package_elf(minimal_elf(), 1, name)

    def test_management_target_and_push_target_transmitted(self):
        records = []
        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
                records.append((self.path, self.headers.get("X-PhotoPainter-App"), body))
                self.send_response(200)
                self.end_headers()
                self.wfile.write(b"ok")
            def log_message(self, *args): pass
        server = HTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        url = "http://127.0.0.1:" + str(server.server_port)
        try:
            for action in ("activate", "switch", "rollback", "remove"):
                self.assertEqual(request(url, "synthetic-token", action, app="clock")[0], 200)
            request(url, "synthetic-token", "stage", b"package", "clock")
            request(url, "synthetic-token", "push", b"png", "clock")
            self.assertEqual(records, [
                ("/api/module?app=clock&op="+op, None, b"") for op in ("activate","switch","rollback","remove")
            ] + [("/api/module?app=clock", None, b"package"), ("/api/push", "clock", b"png")])
        finally:
            server.shutdown(); thread.join(); server.server_close()
