"""Exercise the firmware ELF validator with real artifacts and malformed boundaries."""

import ctypes
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from module import package_elf
from module_test_fixture import minimal_elf

ROOT = Path(__file__).resolve().parent.parent


class ModuleFormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        out = Path(cls.tmp.name) / "format.dylib"
        subprocess.run(
            [
                "cc",
                "-shared",
                "-fPIC",
                "-Wall",
                "-Wextra",
                "-Werror",
                str(ROOT / "main/module_format.c"),
                "-o",
                str(out),
            ],
            check=True,
        )
        cls.lib = ctypes.CDLL(str(out))
        cls.lib.module_elf_valid.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.module_elf_valid.restype = ctypes.c_bool
        cls.elf = minimal_elf()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def valid(self, data):
        return self.lib.module_elf_valid(data, len(data))

    def test_structural_fixture(self):
        self.assertTrue(self.valid(self.elf))

    @unittest.skipUnless(
        os.environ.get("PHOTOFRAME_TEST_ELF"),
        "set PHOTOFRAME_TEST_ELF for artifact integration",
    )
    def test_real_app(self):
        self.assertTrue(
            self.valid(Path(os.environ["PHOTOFRAME_TEST_ELF"]).read_bytes())
        )

    def test_truncations(self):
        for n in [0, 4, 51, 52, len(self.elf) - 1]:
            with self.subTest(n=n):
                self.assertFalse(self.valid(self.elf[:n]))

    def test_out_of_range_section_table(self):
        for v in [len(self.elf), 0xFFFFFFF0]:
            b = bytearray(self.elf)
            struct.pack_into("<I", b, 32, v)
            self.assertFalse(self.valid(bytes(b)))

    def test_wrong_machine_and_endian(self):
        for offset, value in [(5, 2), (18, 3)]:
            b = bytearray(self.elf)
            b[offset] = value
            self.assertFalse(self.valid(bytes(b)))

    def test_section_data_overrun(self):
        b = bytearray(self.elf)
        off = struct.unpack_from("<I", b, 32)[0]
        struct.pack_into("<I", b, off + 40 + 16, 0xFFFFFFF0)
        self.assertFalse(self.valid(bytes(b)))

    def test_package_contract(self):
        import hashlib

        p = package_elf(self.elf, 42)
        self.assertEqual(len(p), 4096 + len(self.elf))
        self.assertEqual(
            struct.unpack_from("<IIIII", p), (0x31464C45, 1, 1, len(self.elf), 42)
        )
        self.assertEqual(p[20:52], hashlib.sha256(self.elf).digest())

    def test_package_rejects_firmware(self):
        with self.assertRaises(ValueError):
            package_elf(b"bad", 1)


if __name__ == "__main__":
    unittest.main()
