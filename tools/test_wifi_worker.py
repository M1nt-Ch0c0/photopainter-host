"""Exercise the firmware's actual Wi-Fi worker using deterministic radio events."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parent.parent


class WiFiWorkerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.binary = str(Path(cls.tmp.name)/"worker")
        subprocess.run(["cc", "-Wall", "-Wextra", "-Werror",
                        "-I"+str(ROOT/"tests/wifi/include"), "-I"+str(ROOT/"tests/slots/include"),
                        "-I"+str(ROOT/"main"), str(ROOT/"tests/wifi/worker_mock.c"),
                        str(ROOT/"main/wifi_station.c"), str(ROOT/"main/wifi_profiles.c"),
                        "-o", cls.binary], check=True)

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()

    def run_scenario(self, scenario):
        return subprocess.check_output([self.binary, str(scenario)], timeout=5, text=True).strip()

    def test_all_unreachable_cycles_without_erasing_profiles(self):
        self.assertEqual(self.run_scenario(0), "1 0:20000 1:20000 2:20000 0:20000 1:20000")

    def test_priority_failure_then_second_success_stops_search(self):
        self.assertEqual(self.run_scenario(1), "0 0:20000 1:20000")

    def test_loss_of_working_second_ap_restarts_from_priority_zero(self):
        self.assertEqual(self.run_scenario(2), "2 0:20000 1:20000 0:20000 1:20000 2:20000")
