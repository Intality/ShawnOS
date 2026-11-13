"""Linux regression tests for the memory test runner's process-group cleanup."""
from contextlib import suppress
import importlib.util
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
import unittest
from unittest.mock import patch


RUNNER = Path(__file__).resolve().parents[1] / "scripts/memory-test.py"
SPEC = importlib.util.spec_from_file_location("memory_test", RUNNER)
MEMORY_TEST = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MEMORY_TEST)

DRIVER = """
import subprocess, sys, time

code = "import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); print('ready', flush=True); time.sleep(30)"
with subprocess.Popen([sys.executable, "-c", code], stdout=subprocess.PIPE, text=True) as child:
    assert child.stdout.readline().strip() == "ready"
    print(child.pid, flush=True)
    if sys.argv[1] == "exit":
        import os
        os._exit(0)
    time.sleep(30)
"""


def child_is_running(pid):
    try:
        stat = Path(f"/proc/{pid}/stat").read_text()
    except FileNotFoundError:
        return False
    return stat.rsplit(")", 1)[1].split()[0] not in {"Z", "X"}


@unittest.skipUnless(sys.platform.startswith("linux"), "requires Linux process groups and /proc")
class MemoryCleanupTest(unittest.TestCase):
    def check_cleanup(self, mode):
        driver = subprocess.Popen([sys.executable, "-c", DRIVER, mode],
                                  stdout=subprocess.PIPE, text=True, start_new_session=True)
        try:
            self.assertTrue(select.select([driver.stdout], [], [], 5)[0], "driver didn't report its child")
            child = int(driver.stdout.readline())
            self.assertEqual(os.getpgid(child), driver.pid)
            if mode == "exit":
                self.assertEqual(driver.wait(timeout=5), 0)
            self.assertTrue(child_is_running(child))

            with patch.object(MEMORY_TEST, "TERMINATE_GRACE_SECONDS", 0.15, create=True):
                MEMORY_TEST.stop_process(driver)
            self.assertIsNotNone(driver.poll(), "driver was not reaped")
            deadline = time.monotonic() + 2
            while child_is_running(child) and time.monotonic() < deadline:
                time.sleep(0.02)
            self.assertFalse(child_is_running(child), "child survived process-group cleanup")
        finally:
            with suppress(ProcessLookupError):
                os.killpg(driver.pid, signal.SIGKILL)
            driver.wait(timeout=5)
            driver.stdout.close()

    def test_child_survives_driver_termination(self):
        self.check_cleanup("wait")

    def test_driver_already_exited(self):
        self.check_cleanup("exit")


if __name__ == "__main__":
    unittest.main()
