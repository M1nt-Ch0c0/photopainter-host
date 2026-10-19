"""Cross-check independent C/Python ABI 2 metadata readers, including malformed sections."""
import ctypes
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from app_manifest import manifest
from module import package_elf
from module_test_fixture import minimal_elf
ROOT=Path(__file__).resolve().parents[1]
class ManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();out=Path(cls.temp.name)/"manifest.dylib"
        subprocess.run(["cc","-shared","-fPIC","-Wall","-Wextra","-Werror",
            str(ROOT/"main/module_format.c"),str(ROOT/"main/app_manifest.c"),"-o",str(out)],check=True)
        cls.lib=ctypes.CDLL(str(out));cls.lib.app_manifest_read.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p]
        cls.lib.app_manifest_read.restype=ctypes.c_bool
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def valid(self,p):return self.lib.app_manifest_read(p,len(p),ctypes.create_string_buffer(128))
    def test_manifest_layout_and_package_abi_selection(self):
        p=minimal_elf("clock",imports="app_host_timer_v2")
        self.assertTrue(self.valid(p));self.assertEqual(manifest(p)["id"],"clock")
        self.assertEqual(struct.unpack_from("<II",package_elf(p,1),4),(2,2))
        with self.assertRaises(ValueError):package_elf(p,1,"other")
        self.assertFalse(self.valid(minimal_elf()))
    def test_unknown_abi_capabilities_reserved_and_nonterminated_strings(self):
        p=minimal_elf("clock");off=struct.unpack_from("<I",p,32)[0]
        m=struct.unpack_from("<I",p,off+6*40+16)[0]
        cases=[(0,0),(4,127),(8,3),(12,1),(16,2),(20,2),(120,1),(124,1)]
        for offset,value in cases:
            b=bytearray(p);struct.pack_into("<I",b,m+offset,value)
            self.assertFalse(self.valid(bytes(b)))
            with self.assertRaises(ValueError):manifest(bytes(b))
        for start,size in ((24,32),(56,64)):
            b=bytearray(p);b[m+start:m+start+size]=b"a"*size
            self.assertFalse(self.valid(bytes(b)))
            with self.assertRaises(ValueError):manifest(bytes(b))
    def test_writable_duplicate_out_of_range_and_disallowed_import(self):
        p=minimal_elf("clock");off=struct.unpack_from("<I",p,32)[0]
        for field,value in ((4,8),(8,3),(16,0xfffffff0),(20,512)):
            b=bytearray(p);struct.pack_into("<I",b,off+6*40+field,value)
            self.assertFalse(self.valid(bytes(b)))
            with self.assertRaises(ValueError):manifest(bytes(b))
        b=bytearray(p);b.extend(p[off+6*40:off+7*40]);struct.pack_into("<H",b,48,8)
        self.assertFalse(self.valid(bytes(b)))
        with self.assertRaises(ValueError):manifest(bytes(b))
        p=minimal_elf("clock",imports="xTaskCreate")
        self.assertFalse(self.valid(p))
        with self.assertRaises(ValueError):manifest(p)
    def test_truncation_at_every_boundary(self):
        p=minimal_elf("clock")
        for end in range(len(p)):
            self.assertFalse(self.valid(p[:end]))
