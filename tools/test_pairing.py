"""Run the production pairing state machine against independent OpenSSL vectors."""
from pathlib import Path
import json
import re
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class PairingTests(unittest.TestCase):
    def test_commit_reveal_physical_binding_and_revocation(self):
        vector = json.loads((ROOT / 'tests/ble/pairing-v2.json').read_text())
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            lines = ['#pragma once', '#include <stdint.h>']
            for name, value in vector.items():
                if isinstance(value, str) and re.fullmatch('[0-9a-f]+', value) and len(value) % 2 == 0:
                    data = bytes.fromhex(value)
                    lines.append('static const uint8_t v_' + name + '[]={' + ','.join(map(str, data)) + '};')
            lines.append('static const char v_name[]=' + json.dumps(vector['name']) + ';')
            (folder/'pairing_vector.h').write_text('\n'.join(lines))
            ssl=Path('/opt/homebrew/opt/openssl@3')
            flags=['-I'+str(ssl/'include'),'-L'+str(ssl/'lib')] if ssl.exists() else []
            binary=folder/'pairing'
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-g',
                '-I'+str(ROOT/'main'),'-I'+str(ROOT/'components/photopainter_board/include'),'-I'+str(folder),str(ROOT/'main/ble_protocol.c'),str(ROOT/'main/pairing.c'),str(ROOT/'main/pairing_screen.c'),
                str(ROOT/'tests/ble/test_pairing.c'),'-lcrypto','-o',str(binary),*flags],check=True)
            subprocess.run([str(binary)],check=True)

    def test_old_http_token_routes_are_retired_before_any_operation(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'http-retired'
            subprocess.run(['cc','-Wall','-Wextra','-Werror',
                '-I'+str(ROOT/'tests/pairing/include'),'-I'+str(ROOT/'tests/runtime/include'),'-I'+str(ROOT/'tests/slots/include'),'-I'+str(ROOT/'main'),
                str(ROOT/'main/push_server.c'),str(ROOT/'tests/pairing/test_http_retired.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_nvs_missing_corrupt_owner_fails_closed_and_reset_persists_tombstone(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory)/'pair-store'
            ssl=Path('/opt/homebrew/opt/openssl@3')
            flags=['-I'+str(ssl/'include'),'-L'+str(ssl/'lib')] if ssl.exists() else []
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-I'+str(ROOT/'tests/slots/include'),'-I'+str(ROOT/'main'),
                str(ROOT/'main/ble_protocol.c'),str(ROOT/'main/pairing.c'),str(ROOT/'main/pairing_store.c'),
                str(ROOT/'tests/pairing/test_store.c'),'-lcrypto','-o',str(binary),*flags],check=True)
            subprocess.run([str(binary)],check=True)
