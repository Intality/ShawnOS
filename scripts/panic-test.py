#!/usr/bin/env python3
"""Boot isolated fault-test ISOs and verify real CPU exception handling."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time

spec = importlib.util.spec_from_file_location("desktop_test", Path(__file__).with_name("desktop-test.py"))
support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(support)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="build/panic-tests")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = (root / args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = [("ud2", "Invalid opcode", 6, 0), ("divide", "Divide error", 0, 0),
             ("pagefault", "Page fault", 14, 2), ("doublefault", "Double fault", 8, 0)]
    results = []
    for case, reason, vector, error in cases:
        folder = output / case
        folder.mkdir(parents=True, exist_ok=True)
        config = folder / "limine.conf"
        config.write_text(f"timeout: 0\n\n/ShawnOS fault test\n    protocol: limine\n"
                          f"    path: boot():/boot/myos.elf\n    resolution: 640x480x32\n"
                          f"    cmdline: selftest={case}\n")
        iso = folder / "myos.iso"
        subprocess.run(["make", "iso", f"ISO={iso}", f"LIMINE_CONFIG={config}"],
                       cwd=root, check=True, timeout=120)
        serial = folder / "serial.log"
        serial.write_text("")
        with tempfile.TemporaryDirectory(prefix="shawnos-panic-") as temp, (folder / "qemu.log").open("w") as log:
            process = subprocess.Popen([
                "qemu-system-x86_64", "-M", "q35", "-m", "128M", "-cdrom", str(iso),
                "-boot", "d", "-display", "none", "-serial", f"file:{serial}",
                "-qmp", f"unix:{temp}/qmp,server=on,wait=off", "-no-reboot", "-no-shutdown",
            ], stdout=subprocess.DEVNULL, stderr=log)
            qmp = None
            try:
                qmp = support.QMP(Path(temp) / "qmp", process)
                deadline = time.monotonic() + 60
                while "MYOS: panic halted" not in serial.read_text(errors="replace"):
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(f"{case}: missing panic marker\n{serial.read_text(errors='replace')}")
                    time.sleep(0.1)
                text = serial.read_text()
                assert text.count("MYOS: kernel entry") == 1, "Kernel rebooted"
                assert f"MYOS: PANIC {reason}\n" in text, text
                assert f"Vector=0x{vector:016x}" in text, text
                assert f"Error=0x{error:016x}" in text, text
                assert re.search(r"RIP=0x[1-9a-f][0-9a-f]{15}", text), "Missing saved instruction address"
                if case == "pagefault":
                    assert "CR2=0x0000000000000000" in text, text
                if case == "doublefault":
                    assert "MYOS: double fault emergency stack" in text, text
                time.sleep(0.1)
                registers = qmp.execute("human-monitor-command", **{"command-line": "info registers"})
                (folder / "registers.txt").write_text(registers)
                assert "HLT=1" in registers, "Fault did not halt the CPU"
                picture = folder / "panic.ppm"
                qmp.execute("screendump", filename=str(picture))
                width, height, pixels = support.read_ppm(picture)
                assert (width, height) == (640, 480)
                assert pixels[:3] == bytes.fromhex("c85b66"), "Missing panic stripe"
                assert pixels[12 * width * 3:12 * width * 3 + 3] == bytes.fromhex("252b29"), "Missing panic background"
                assert bytes.fromhex("f5f8f6") in pixels, "Missing panic text"
                qmp.execute("quit")
                process.wait(timeout=10)
                results.append({"case": case, "vector": vector, "error": error, "result": "passed"})
                print(f"Panic test passed: {case}, vector {vector}, halted with diagnostic screen.", flush=True)
            finally:
                if qmp is not None:
                    qmp.close()
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    (output / "result.json").write_text(json.dumps({"version": "0.3.1", "checks": results}, indent=2) + "\n")


if __name__ == "__main__":
    main()
