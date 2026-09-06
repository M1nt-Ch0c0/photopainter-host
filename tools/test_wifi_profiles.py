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


class HostConfig(ctypes.Structure):
    _fields_ = [("wifi", Profiles), ("token", ctypes.c_char * 129)]


class WiFiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        out = Path(cls.tmp.name) / "wifi.dylib"
        cjson = ROOT / "managed_components/espressif__cjson/cJSON"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "tests/slots/include"), "-I" + str(cjson),
                        str(ROOT / "main/wifi_profiles.c"), str(ROOT / "main/wifi_profiles_json.c"),
                        "-I" + str(ROOT / "main"), str(ROOT / "main/host_config.c"),
                        str(ROOT / "tests/wifi/config_mock.c"),
                        str(cjson / "cJSON.c"), "-o", str(out)], check=True)
        cls.lib = ctypes.CDLL(str(out))
        cls.lib.wifi_profiles_parse.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(Profiles)]
        cls.lib.wifi_profiles_read_file.argtypes = [ctypes.c_char_p, ctypes.POINTER(Profiles)]
        cls.lib.wifi_profiles_valid.argtypes = [ctypes.POINTER(Profiles)]
        cls.lib.wifi_profiles_valid.restype = ctypes.c_bool
        cls.lib.wifi_profiles_save_file.argtypes = [ctypes.c_char_p, ctypes.POINTER(Profiles)]
        cls.lib.wifi_profiles_migrate.argtypes = [ctypes.c_char_p, ctypes.POINTER(Profiles), ctypes.POINTER(Profiles)]
        cls.lib.photopainter_host_config_load.argtypes = [ctypes.POINTER(HostConfig)]

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

    def test_atomic_save_roundtrip_and_recover_backup_generation(self):
        pairs = [("home", "password"), ("office", "")]
        profiles = Profiles.from_buffer_copy(profiles_blob(pairs))
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/"wifi.json"
            self.assertEqual(self.lib.wifi_profiles_save_file(str(path).encode(), ctypes.byref(profiles)), 0)
            self.assertEqual(load_profiles(path), pairs)
            # Simulate reset after old JSON renamed to backup, before publishing temp.
            path.rename(str(path)+".bak")
            Path(str(path)+".tmp").write_text("partial new generation")
            out = Profiles()
            self.assertEqual(self.lib.wifi_profiles_read_file(str(path).encode(), ctypes.byref(out)), 0)
            self.assertEqual(bytes(out), bytes(profiles))
            self.assertEqual(self.lib.wifi_profiles_save_file(str(path).encode(), ctypes.byref(profiles)), 0)
            self.assertFalse(Path(str(path)+".bak").exists())
            self.assertFalse(Path(str(path)+".tmp").exists())

    def test_first_migration_prefers_nvs_and_preserves_explicit_empty_list(self):
        for pairs in ([("nvs-network", "password")], []):
            with tempfile.TemporaryDirectory() as d:
                root = Path(d); (root/"wifi.txt").write_text("legacy\npassword\n")
                fallback = Profiles.from_buffer_copy(profiles_blob(pairs)); out = Profiles()
                self.assertEqual(self.lib.wifi_profiles_migrate(str(root).encode(), ctypes.byref(fallback), ctypes.byref(out)), 0)
                self.assertEqual(load_profiles(root/"config/wifi.json"), pairs)
                self.assertEqual(bytes(out), bytes(fallback))
                self.assertEqual((root/"wifi.txt").read_text(), "legacy\npassword\n")

    def test_first_migration_accepts_legacy_crlf_and_open_network(self):
        for text, expected in [(b"legacy\r\npassword\r\n", [("legacy","password")]),
                               (b"open-network\n\n", [("open-network","")])]:
            with tempfile.TemporaryDirectory() as d:
                root=Path(d); (root/"wifi.txt").write_bytes(text); out=Profiles()
                self.assertEqual(self.lib.wifi_profiles_migrate(str(root).encode(), None, ctypes.byref(out)),0)
                self.assertEqual(load_profiles(root/"config/wifi.json"), expected)

    def test_migration_never_overwrites_existing_empty_or_broken_json(self):
        fallback=Profiles.from_buffer_copy(profiles_blob([("fallback","password")]))
        for content, expected_ok in [(b'{"version":1,"networks":[]}', True),(b'broken',False)]:
            with tempfile.TemporaryDirectory() as d:
                root=Path(d); (root/"config").mkdir(); p=root/"config/wifi.json";p.write_bytes(content)
                out=Profiles()
                result=self.lib.wifi_profiles_migrate(str(root).encode(),ctypes.byref(fallback),ctypes.byref(out))
                self.assertEqual(result==0,expected_ok)
                self.assertEqual(p.read_bytes(),content)
                if expected_ok:self.assertEqual(out.count,0)

    def test_bad_legacy_and_failed_directory_write_do_not_publish_json(self):
        for text in (b'bad\x00ssid\npassword',b'only-one-line',b'x'*33+b'\npassword',b'ssid\nshort',b'ssid\npassword\nextra'):
            with tempfile.TemporaryDirectory() as d:
                root=Path(d);(root/"wifi.txt").write_bytes(text);out=Profiles()
                self.assertNotEqual(self.lib.wifi_profiles_migrate(str(root).encode(),None,ctypes.byref(out)),0)
                self.assertFalse((root/"config/wifi.json").exists())
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);(root/"config").write_text("not a directory")
            fallback=Profiles.from_buffer_copy(profiles_blob([("nvs","password")]))
            self.assertNotEqual(self.lib.wifi_profiles_migrate(str(root).encode(),ctypes.byref(fallback),ctypes.byref(Profiles())),0)
            self.assertEqual((root/"config").read_text(),"not a directory")

    def test_config_arbitrates_sd_nvs_legacy_and_errors(self):
        for mode, count, ssid in [(0,0,b""),(1,1,b"legacy"),(2,2,b"first"),(3,0,b"first"),
                                 (5,1,b"from-sd"),(6,0,b""),(9,1,b"from-sd"),(10,1,b"legacy")]:
            with self.subTest(mode=mode):
                self.lib.mock_config_mode(mode); config=HostConfig()
                self.assertEqual(self.lib.photopainter_host_config_load(ctypes.byref(config)),0)
                self.assertEqual(config.wifi.count,count)
                if count:self.assertEqual(config.wifi.items[0].ssid,ssid)
                self.assertEqual(bool(config.token),mode not in (0,9))
        for mode in (4,7,8,11,12):
            with self.subTest(error_mode=mode):
                self.lib.mock_config_mode(mode)
                self.assertNotEqual(self.lib.photopainter_host_config_load(ctypes.byref(HostConfig())),0)
