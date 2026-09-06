"""Run the actual C SD JSON parser, plus the matching NVS provisioning codec."""
import ctypes
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from provision import load_profiles, profiles_blob, validate_network

ROOT = Path(__file__).resolve().parent.parent


class Profile(ctypes.Structure):
    _fields_ = [("ssid", ctypes.c_char * 33), ("password", ctypes.c_char * 65)]


class Profiles(ctypes.Structure):
    _fields_ = [("version", ctypes.c_uint32), ("count", ctypes.c_uint32), ("items", Profile * 10)]


class WiFiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        out = Path(cls.tmp.name) / "wifi.dylib"
        cjson = ROOT / "managed_components/espressif__cjson/cJSON"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "tests/slots/include"), "-I" + str(cjson),
                        str(ROOT / "main/wifi_profiles.c"), str(ROOT / "main/wifi_profiles_json.c"),
                        str(cjson / "cJSON.c"), "-o", str(out)], check=True)
        cls.lib = ctypes.CDLL(str(out))
        cls.lib.wifi_profiles_parse.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(Profiles)]
        cls.lib.wifi_profiles_read_file.argtypes = [ctypes.c_char_p, ctypes.POINTER(Profiles)]
        cls.lib.wifi_profiles_valid.argtypes = [ctypes.POINTER(Profiles)]
        cls.lib.wifi_profiles_valid.restype = ctypes.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def parse(self, data):
        p = Profiles()
        e = self.lib.wifi_profiles_parse(data, len(data), ctypes.byref(p))
        return e, p

    def test_order_and_nvs_binary_contract(self):
        pairs = [("home", "password1"), ("office", "password2"), ("开放网络", "")]
        data = json.dumps({"version": 1, "networks": [dict(ssid=s, password=p) for s, p in pairs]}).encode()
        err, p = self.parse(data)
        self.assertEqual(err, 0)
        self.assertEqual(p.count, 3)
        self.assertEqual([i.ssid.decode() for i in p.items[:3]], [s for s, _ in pairs])
        self.assertEqual(bytes(p), profiles_blob(pairs))
        self.assertTrue(self.lib.wifi_profiles_valid(ctypes.byref(p)))

    def test_empty_list_authoritative(self):
        err, p = self.parse(b'{"version":1,"networks":[]}')
        self.assertEqual((err, p.count), (0, 0))

    def test_rejects_malformed_duplicate_overflow_and_nul(self):
        bodies = [b'{}', b'{"version":2,"networks":[]}', b'{"version":1,"version":1,"networks":[]}',
                  b'{"version":1,"networks":[{"ssid":"a","password":"","ssid":"b"}]}',
                  b'{"version":1,"networks":[{"ssid":"a\\u0000b","password":""}]}',
                  b'{"version":1,"networks":[]} trailing', b'[' * 5000, b' ' * 8193,
                  b'{"version":1,"networks":[]}\0',
                  json.dumps({"version":1, "networks":[{"ssid":"same", "password":""}]*2}).encode(),
                  json.dumps({"version":1, "networks":[{"ssid":str(i), "password":""} for i in range(11)]}).encode()]
        for body in bodies:
            with self.subTest(body=body[:80]): self.assertNotEqual(self.parse(body)[0], 0)

    def test_password_and_ssid_boundaries_match_python(self):
        for ssid, password, valid in [("s"*32,"f"*64,True),("s"*33,"",False),
                ("home","z"*64,False),("home","short",False),("home","",True),
                ("home","a"*63,True),("home","中"*8,False),("bad\nssid","password",False)]:
            data = json.dumps({"version":1,"networks":[dict(ssid=ssid,password=password)]}).encode()
            self.assertEqual(self.parse(data)[0] == 0, valid)
            if valid: validate_network(ssid, password)
            else:
                with self.assertRaises(ValueError): validate_network(ssid, password)

    def test_backup_only_when_primary_absent_and_never_rewrites_corruption(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/"wifi.json"
            backup = Path(str(path)+".bak")
            backup.write_text('{"version":1,"networks":[]}')
            p = Profiles()
            self.assertEqual(self.lib.wifi_profiles_read_file(str(path).encode(), ctypes.byref(p)), 0)
            path.write_text('broken')
            self.assertNotEqual(self.lib.wifi_profiles_read_file(str(path).encode(), ctypes.byref(p)), 0)
            self.assertEqual(path.read_text(), 'broken')

    def test_python_loader_accepts_original_schema_and_rejects_duplicates(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/"wifi.json"
            path.write_text('{"version":1,"networks":[{"ssid":"home","password":""}]}')
            self.assertEqual(load_profiles(path), [("home", "")])
            path.write_text('{"version":1,"version":1,"networks":[]}')
            with self.assertRaises(ValueError): load_profiles(path)
