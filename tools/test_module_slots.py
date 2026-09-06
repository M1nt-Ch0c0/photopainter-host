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


class Entry(ctypes.Structure):
    _fields_ = [("id", ctypes.c_char * 32), ("slots", State)]


class Catalog(ctypes.Structure):
    _fields_ = [("version", ctypes.c_uint32), ("selected", ctypes.c_int32),
                ("trial_app", ctypes.c_int32), ("apps", Entry * 5)]


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
            str(ROOT / "main/photoframe_plugin.c"),
            str(ROOT / "tests/slots/plugin_mock.c"),
            "-lcrypto",
            "-o",
            str(out),
        ]
        subprocess.run(args, check=True)
        cls.lib = ctypes.CDLL(str(out))
        cls.lib.module_slots_state.restype = State
        cls.lib.module_slots_stage.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.app_catalog_get.restype = ctypes.POINTER(Catalog)
        cls.lib.app_catalog_stage.argtypes = [ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.app_catalog_begin.argtypes = [ctypes.c_int, ctypes.c_bool]
        cls.lib.app_catalog_find.argtypes = [ctypes.c_char_p]
        cls.lib.mock_seed.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
        cls.lib.photoframe_plugin_select.argtypes = [ctypes.c_char_p, ctypes.c_bool]
        cls.lib.photoframe_plugin_render.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
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

    def stage_app(self, name, version=1):
        package = package_elf(minimal_elf(), version, name)
        return self.lib.app_catalog_stage(name.encode(), package, len(package))

    def catalog(self):
        return self.lib.app_catalog_get().contents

    def test_independent_install_update_switch_and_rollback(self):
        self.assertEqual(self.stage_app("clock"), 0)
        self.assertEqual(self.stage_app("weather"), 0)
        self.assertEqual(self.catalog().selected, 0)
        self.assertEqual(self.lib.app_catalog_begin(1, True), 0)
        self.assertEqual(self.lib.module_slots_confirm(), 0)
        self.assertEqual(self.catalog().selected, 1)
        self.assertEqual(self.stage_app("clock", 2), 0)
        self.assertEqual(self.lib.app_catalog_begin(1, True), 0)
        self.assertEqual(self.lib.module_slots_confirm(), 0)
        clock = self.catalog().apps[1].slots
        self.assertEqual((clock.active, clock.previous), (1, 0))
        self.assertEqual(self.catalog().apps[2].slots.pending, 0)
        self.assertEqual(self.lib.app_catalog_begin(0, False), 0)
        self.assertEqual(self.lib.module_slots_confirm(), 0)
        self.assertEqual(self.lib.app_catalog_rollback(1), 0)
        self.assertEqual(self.catalog().selected, 0)
        self.assertEqual(self.catalog().apps[1].slots.active, 0)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.catalog().apps[1].slots.active, 0)

    def test_switch_trial_reset_restores_original_application(self):
        self.assertEqual(self.stage_app("clock"), 0)
        self.assertEqual(self.lib.app_catalog_begin(1, True), 0)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.catalog().selected, 0)
        self.assertEqual(self.catalog().trial_app, -1)
        self.assertEqual(self.catalog().apps[1].slots.active, -1)
        self.assertEqual(self.stage_app("clock", 2), 0)
        self.assertEqual(self.lib.app_catalog_begin(1, True), 0)
        self.assertEqual(self.lib.module_slots_confirm(), 0)
        self.assertEqual(self.lib.app_catalog_begin(0, False), 0)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.catalog().selected, 1)

    def test_capacity_reuse_and_current_app_cannot_be_removed(self):
        for name in ("clock", "weather", "calendar", "notes"):
            self.assertEqual(self.stage_app(name), 0)
        self.assertNotEqual(self.stage_app("extra"), 0)
        self.assertNotEqual(self.lib.app_catalog_remove(0), 0)
        self.assertEqual(self.lib.app_catalog_remove(2), 0)
        self.assertEqual(self.stage_app("extra"), 0)
        self.assertEqual(self.lib.app_catalog_find(b"extra"), 2)
        self.assertEqual(self.lib.app_catalog_find(b"weather"), -1)

    def test_cross_app_package_rejected_without_allocating(self):
        p = package_elf(minimal_elf(), 1, "clock")
        self.assertNotEqual(self.lib.app_catalog_stage(b"weather", p, len(p)), 0)
        self.assertEqual(self.lib.app_catalog_find(b"weather"), -1)
        self.assertNotEqual(self.lib.app_catalog_stage(b"clock", self.package, len(self.package)), 0)

    def test_failed_new_app_write_does_not_touch_other_apps(self):
        self.assertEqual(self.stage(), 0)
        self.lib.mock_fail_write(1)
        self.assertNotEqual(self.stage_app("clock"), 0)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.state(), (0, -1, 1, 0))
        self.assertEqual(self.stage_app("clock"), 0)

    def test_one_trial_globally_and_failed_confirm_preserves_selection(self):
        self.assertEqual(self.stage_app("clock"), 0)
        self.assertEqual(self.lib.app_catalog_begin(1, True), 0)
        self.assertNotEqual(self.lib.app_catalog_begin(0, False), 0)
        self.assertNotEqual(self.stage_app("weather"), 0)
        self.lib.mock_fail_commit(1)
        self.assertNotEqual(self.lib.module_slots_confirm(), 0)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.catalog().selected, 0)

    def init_plugin(self):
        self.lib.mock_seed(self.package, len(self.package))
        self.lib.mock_plugin_result(0, 1)
        self.assertEqual(self.lib.photoframe_plugin_init(), 0)
        self.assertEqual(self.lib.mock_plugin_live(), 1)

    def test_loader_switch_failure_restores_previous_app_and_only_one_elf(self):
        self.init_plugin()
        self.assertEqual(self.stage_app("clock"), 0)
        self.lib.mock_plugin_fail_load()
        self.assertNotEqual(self.lib.photoframe_plugin_select(b"clock", True), 0)
        self.assertEqual(self.lib.photoframe_plugin_app(), 0)
        self.assertEqual(self.catalog().selected, 0)
        self.assertEqual(self.lib.mock_plugin_live(), 1)
        self.assertEqual(self.lib.mock_plugin_peak(), 1)

    def test_loader_rejected_image_does_not_confirm_and_display_failure_restores(self):
        self.init_plugin()
        self.assertEqual(self.stage_app("clock"), 0)
        self.assertEqual(self.lib.photoframe_plugin_select(b"clock", True), 0)
        self.lib.mock_plugin_result(-2, 1)
        self.assertEqual(self.lib.photoframe_plugin_render(b"png", 3), -2)
        self.assertEqual(self.catalog().trial_app, 1)
        self.lib.mock_plugin_result(-6, 1)
        self.assertEqual(self.lib.photoframe_plugin_render(b"png", 3), -6)
        self.assertEqual(self.lib.photoframe_plugin_app(), 0)
        self.assertEqual(self.catalog().trial_app, -1)
        self.assertEqual(self.lib.mock_plugin_peak(), 1)

    def test_loader_success_confirms_new_app_missing_callback_restores_on_switch(self):
        self.init_plugin()
        self.assertEqual(self.stage_app("clock"), 0)
        self.assertEqual(self.lib.photoframe_plugin_select(b"clock", True), 0)
        self.assertEqual(self.lib.photoframe_plugin_render(b"png", 3), 0)
        self.assertEqual(self.catalog().selected, 1)
        self.assertEqual(self.lib.photoframe_plugin_select(b"photoframe", False), 0)
        self.lib.mock_plugin_result(0, 0)
        self.assertEqual(self.lib.photoframe_plugin_render(b"png", 3), -8)
        self.assertEqual(self.lib.photoframe_plugin_app(), 1)
        self.assertEqual(self.catalog().selected, 1)
        self.assertEqual(self.lib.mock_plugin_peak(), 1)

    def test_legacy_journal_migration_preserves_confirmed_versions(self):
        self.lib.mock_legacy(1, 0, -1, 0)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.state(), (1, 0, -1, 0))
        self.assertEqual(self.catalog().apps[0].id, b"photoframe")
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.state(), (1, 0, -1, 0))

    def test_legacy_unfinished_trial_is_cancelled_on_migration(self):
        self.lib.mock_legacy(0, -1, 1, 1)
        self.assertEqual(self.lib.module_slots_init(), 0)
        self.assertEqual(self.state(), (0, -1, -1, 0))

    def test_damaged_catalog_does_not_fall_back_to_stale_legacy(self):
        self.lib.mock_corrupt_catalog()
        self.assertNotEqual(self.lib.module_slots_init(), 0)
        self.assertFalse(self.lib.app_catalog_get())
        self.assertNotEqual(self.stage_app("clock"), 0)

    def test_corrupted_candidate_data_cannot_load_and_original_still_runs(self):
        self.init_plugin()
        self.assertEqual(self.stage_app("clock"), 0)
        self.lib.mock_corrupt_payload(1, 0)
        self.assertNotEqual(self.lib.photoframe_plugin_select(b"clock", True), 0)
        self.assertEqual(self.lib.photoframe_plugin_app(), 0)
        self.assertEqual(self.lib.mock_plugin_live(), 1)


if __name__ == "__main__":
    unittest.main()
