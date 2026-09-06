import ctypes
from pathlib import Path
import subprocess
import tempfile
import unittest
from module import package_elf
from module_test_fixture import minimal_elf

ROOT = Path(__file__).resolve().parent.parent


class State(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int32) for n in ["active", "previous", "pending", "trial"]]


class SlotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        out = Path(cls.temp.name) / "slots.dylib"
        ssl = Path("/opt/homebrew/opt/openssl@3")
        args = [
            "cc",
            "-shared",
            "-fPIC",
            "-I" + str(ROOT / "tests/slots/include"),
            "-I" + str(ROOT / "main"),
        ]
        if ssl.exists():
            args += ["-I" + str(ssl / "include"), "-L" + str(ssl / "lib")]
        args += [
            str(ROOT / "tests/slots/mock.c"),
            str(ROOT / "main/module_slots.c"),
            str(ROOT / "main/module_format.c"),
            "-lcrypto",
            "-o",
            str(out),
        ]
        subprocess.run(args, check=True)
        cls.lib = ctypes.CDLL(str(out))
        cls.lib.module_slots_state.restype = State
        cls.lib.module_slots_stage.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.package = package_elf(minimal_elf(), 10)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.mock_reset()
        self.assertEqual(self.lib.module_slots_init(), 0)

    def stage(self):
        return self.lib.module_slots_stage(self.package, len(self.package))

    def state(self):
        s = self.lib.module_slots_state()
        return (s.active, s.previous, s.pending, s.trial)

    def test_trial_reset_reverts(self):
        self.assertEqual(self.stage(), 0)
        self.assertEqual(self.lib.module_slots_begin_trial(), 0)
        self.assertEqual(self.state(), (0, -1, 1, 1))
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.state(), (0, -1, -1, 0))

    def test_confirm_and_manual_rollback(self):
        self.assertEqual(self.stage(), 0)
        self.assertEqual(self.lib.module_slots_begin_trial(), 0)
        self.assertEqual(self.lib.module_slots_confirm(), 0)
        self.assertEqual(self.state(), (1, 0, -1, 0))
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.state(), (1, 0, -1, 0))
        self.assertEqual(self.lib.module_slots_rollback(), 0)
        self.assertEqual(self.state(), (0, 1, -1, 0))

    def test_interrupted_payload_or_header_write_preserves_active(self):
        for n in [1, 2]:
            with self.subTest(write=n):
                self.setUp()
                self.lib.mock_fail_write(n)
                self.assertNotEqual(self.stage(), 0)
                self.assertEqual(self.lib.module_slots_init(), 0)
                self.assertEqual(self.state(), (0, -1, -1, 0))

    def test_pending_metadata_commit_failure_does_not_activate(self):
        self.lib.mock_fail_commit(2)
        self.assertNotEqual(self.stage(), 0)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.state(), (0, -1, -1, 0))

    def test_confirmation_commit_failure_reverts_on_boot(self):
        self.assertEqual(self.stage(), 0)
        self.lib.module_slots_begin_trial()
        self.lib.mock_fail_commit(1)
        self.assertNotEqual(self.lib.module_slots_confirm(), 0)
        self.lib.module_slots_init()
        self.assertEqual(self.state(), (0, -1, -1, 0))

    def test_corrupt_hash_rejected_without_mutation(self):
        b = bytearray(self.package)
        b[20] ^= 1
        self.assertNotEqual(self.lib.module_slots_stage(bytes(b), len(b)), 0)
        self.assertEqual(self.state(), (0, -1, -1, 0))

    def test_cannot_overwrite_a_pending_trial(self):
        self.assertEqual(self.stage(), 0)
        self.assertNotEqual(self.stage(), 0)
        self.lib.module_slots_begin_trial()
        self.assertNotEqual(self.stage(), 0)


if __name__ == "__main__":
    unittest.main()
