"""Actual C button policy, services and board packer against deterministic platform stubs."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class RuntimeServiceTests(unittest.TestCase):
    def run_native(self,name,sources):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/name
            includes=["tests/runtime/include","tests/slots/include","main",
                "managed_components/espressif__cjson/cJSON","components/photopainter_board/include","components/photopainter_board/private_include"]
            subprocess.run(["cc","-pthread","-Wall","-Wextra","-Werror"]+
                ["-I"+str(ROOT/p) for p in includes]+
                [str(ROOT/p) for p in sources]+["-o",str(out)],check=True)
            subprocess.run([str(out)],check=True)
    def test_button_debounce_busy_epoch_and_boot_held(self):
        self.run_native("button",["main/app_button.c","tests/runtime/test_button.c"])
    def test_services_cache_display_timers_and_watchdog(self):
        self.run_native("services",["main/app_services.c","components/photopainter_board/src/board.c","tests/runtime/test_services.c"])

    def test_manager_serialization_coalescing_snapshot_and_input_lifetime(self):
        self.run_native("manager",["main/app_manager.c","main/app_button.c",
            "tests/runtime/test_manager.c","managed_components/espressif__cjson/cJSON/cJSON.c"])
