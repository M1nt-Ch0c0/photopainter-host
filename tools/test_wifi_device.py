"""Exercise credential backup protection and the real HTTP transport with a local server."""
import http.server
import json
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
import urllib.error
import wifi_device

BODY = b'{"version":1,"networks":[{"ssid":"test","password":"password"}]}'

class WiFiDeviceTests(unittest.TestCase):
    def test_backup_private_validated_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as d, patch('wifi_device.transfer', return_value=BODY):
            path = Path(d)/'backup.json'
            self.assertEqual(wifi_device.backup('http://device', 'token', path), 1)
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(path.read_bytes(), BODY)
            with self.assertRaises(ValueError): wifi_device.backup('http://device', 'token', path)
            link = Path(d)/'link'; link.symlink_to(path)
            with self.assertRaises(ValueError): wifi_device.backup('http://device', 'token', link)
            self.assertEqual(path.read_bytes(), BODY)

    def test_invalid_backup_never_published_or_left_in_temporary(self):
        with tempfile.TemporaryDirectory() as d, patch('wifi_device.transfer', return_value=b'broken'):
            with self.assertRaises(ValueError): wifi_device.backup('http://device', 'token', Path(d)/'backup')
            self.assertEqual(list(Path(d).iterdir()), [])

    def test_update_validates_before_network_and_preserves_order(self):
        with tempfile.TemporaryDirectory() as d, patch('wifi_device.transfer') as transfer:
            source = Path(d)/'wifi.json'; source.write_bytes(b'broken')
            with self.assertRaises(ValueError): wifi_device.update('http://device', 'token', source)
            transfer.assert_not_called()
            source.write_bytes(BODY)
            self.assertEqual(wifi_device.update('http://device', 'token', source), 1)
            self.assertEqual(json.loads(transfer.call_args.args[2]), json.loads(BODY))

    def test_http_method_auth_bounds_no_proxy_or_redirect(self):
        seen = []
        class Handler(http.server.BaseHTTPRequestHandler):
            mode = 'ok'
            def log_message(self, *args): pass
            def do_GET(self):
                seen.append((self.command, self.path, self.headers.get('Authorization')))
                self.send_response(302 if self.mode == 'redirect' else 200)
                if self.mode == 'redirect': self.send_header('Location', '/secret-leak')
                self.end_headers()
                self.wfile.write(b'x'*8193 if self.mode == 'large' else BODY)
            do_POST = do_GET
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
        base = f'http://127.0.0.1:{server.server_port}'
        try:
            with patch.dict(os.environ, {'http_proxy':'http://127.0.0.1:1', 'HTTP_PROXY':'http://127.0.0.1:1', 'no_proxy':'', 'NO_PROXY':''}):
                self.assertEqual(wifi_device.transfer(base, 'test-token'), BODY)
                wifi_device.transfer(base, 'test-token', BODY)
            self.assertEqual(seen, [('GET','/api/wifi','Bearer test-token'),('POST','/api/wifi','Bearer test-token')])
            Handler.mode = 'redirect'
            with self.assertRaises(urllib.error.HTTPError): wifi_device.transfer(base, 'test-token')
            self.assertEqual(len(seen), 3)
            Handler.mode = 'large'
            with self.assertRaises(ValueError): wifi_device.transfer(base, 'test-token')
        finally:
            server.shutdown(); server.server_close(); thread.join()
