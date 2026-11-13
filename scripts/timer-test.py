#!/usr/bin/env python3
"""Verify repeated APIC/PIC boot, actual idle wakes, and spurious return."""
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
    parser.add_argument("--output-dir", default="build/timer-tests")
    parser.add_argument("--boot-repeats", type=int, default=3)
    parser.add_argument("--trace-interrupts", action="store_true")
    parser.add_argument("--tcg-thread", choices=["single", "multi"])
    args = parser.parse_args()
    if args.boot_repeats < 1:
        parser.error("--boot-repeats must be positive")
    root = Path(__file__).resolve().parents[1]
    output = (root / args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.json").unlink(missing_ok=True)
    cases = []
    for trial in range(1, args.boot_repeats + 1):
        cases.extend([(f"apic-{trial}", "q35", "qemu64", "4", "", "xAPIC PIC virtual wire ready"),
                      (f"legacy-{trial}", "pc", "qemu64,-apic", "1", "", "legacy PIC delivery ready")])
    cases.extend([("no-tsc", "q35", "qemu64,-tsc", "1", "", "xAPIC PIC virtual wire ready"),
                  ("spurious", "q35", "qemu64", "1", "selftest=apic-spurious", "xAPIC PIC virtual wire ready")])
    results = []
    for name, machine, cpu, smp, command_line, route in cases:
        folder = output / name
        folder.mkdir(parents=True, exist_ok=True)
        config = folder / "limine.conf"
        config.write_text("timeout: 0\n\n/ShawnOS timer test\n    protocol: limine\n"
                          "    path: boot():/boot/myos.elf\n    resolution: 640x480x32\n"
                          f"    cmdline: {command_line}\n")
        iso = folder / "myos.iso"
        subprocess.run(["make", "iso", f"ISO={iso}", f"LIMINE_CONFIG={config}"],
                       cwd=root, check=True, timeout=120)
        serial = folder / "serial.log"
        serial.write_text("")
        with tempfile.TemporaryDirectory(prefix="shawnos-timer-") as temp, (folder / "qemu.log").open("w") as log:
            process = subprocess.Popen([
                "qemu-system-x86_64", "-M", machine, "-cpu", cpu, "-smp", smp,
                "-m", "128M", "-cdrom", str(iso), "-boot", "d", "-display", "none",
                "-serial", f"file:{serial}", "-qmp", f"unix:{temp}/qmp,server=on,wait=off",
                "-no-reboot", "-no-shutdown",
                *(["-accel", f"tcg,thread={args.tcg_thread}"] if args.tcg_thread else []),
                *(["-d", "int", "-D", str(folder / "interrupts.log")] if args.trace_interrupts else []),
            ], stdout=subprocess.DEVNULL, stderr=log)
            qmp = None
            try:
                qmp = support.QMP(Path(temp) / "qmp", process)

                def text():
                    return serial.read_text(errors="replace")

                def wait(marker, count=1):
                    deadline = time.monotonic() + 60
                    while text().count(marker) < count:
                        if process.poll() is not None or "MYOS: panic halted" in text() or time.monotonic() > deadline:
                            raise RuntimeError(f"{name}: missing {marker}\n{text()}")
                        time.sleep(0.1)

                wait("MYOS: " + route)
                wait("MYOS: boot ok v0.4.0")
                wait("MYOS: timer ticked")
                if name == "spurious":
                    wait("MYOS: APIC spurious self-test passed")

                def key(code):
                    qmp.execute("send-key", keys=[{"type": "qcode", "data": code}], **{"hold-time": 20})
                    time.sleep(0.08)

                key("f1")
                wait("MYOS: open Terminal")
                for sample in range(2):
                    for code in ["u", "p", "t", "i", "m", "e", "ret"]:
                        key(code)
                    wait("MYOS: uptime seconds=", sample + 1)
                    time.sleep(1.1)
                samples = re.findall(r"MYOS: uptime seconds=(\d+) ticks=(\d+) idle=(\d+)", text())
                assert len(samples) == 2, text()
                before, after = [tuple(map(int, sample)) for sample in samples]
                assert all(a > b > 0 for a, b in zip(after, before)), "Timer/input/idle did not advance"
                assert text().count("MYOS: kernel entry") == 1, "Kernel rebooted"
                qmp.execute("screendump", filename=str(folder / "screen.ppm"))
                width, height, pixels = support.read_ppm(folder / "screen.ppm")
                assert (width, height) == (640, 480)
                qmp.execute("quit")
                process.wait(timeout=10)
                results.append({"case": name, "route": route, "result": "passed"})
                print(f"Timer regression passed: {name}.", flush=True)
            except BaseException:
                if qmp is not None and process.poll() is None:
                    try:
                        for info in ["registers", "pic", "lapic", "irq"]:
                            diagnostic = qmp.execute("human-monitor-command", **{"command-line": f"info {info}"})
                            (folder / f"failure-{info}.txt").write_text(diagnostic)
                    except Exception:
                        pass
                raise
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
    (output / "result.json").write_text(json.dumps({"version": "0.4.0", "checks": results}, indent=2) + "\n")


if __name__ == "__main__":
    main()
