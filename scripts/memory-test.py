#!/usr/bin/env python3
"""Verify full physical-frame capacity and desktop behavior in fresh QEMU boots."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


TERMINATE_GRACE_SECONDS = 10


def process_rss(pid):
    try:
        status = Path(f"/proc/{pid}/status").read_text()
        own = next(int(line.split()[1]) for line in status.splitlines() if line.startswith("VmRSS:"))
        children = Path(f"/proc/{pid}/task/{pid}/children").read_text().split()
        return own + sum(process_rss(int(child)) for child in children)
    except (FileNotFoundError, ProcessLookupError, StopIteration):
        return 0


def stop_process(process):
    # The caller starts a dedicated session. Its group can outlive the driver.
    def signal_group(sig):
        try:
            os.killpg(process.pid, sig)
            return True
        except ProcessLookupError:
            return False

    if signal_group(signal.SIGTERM):
        deadline = time.monotonic() + TERMINATE_GRACE_SECONDS
        while True:
            process.poll()
            if not signal_group(0):
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                signal_group(signal.SIGKILL)
                break
            time.sleep(min(0.05, remaining))
    process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--memory", nargs="+", type=int,
                        default=[64, 128, 256, 512, 768, 1023, 1024, 1025, 1536, 2048, 4096, 8192, 12288, 16384],
                        help="Assigned RAM sizes in 1024-based MB")
    parser.add_argument("--resolution", choices=["1024x768", "640x480"], default="1024x768")
    parser.add_argument("--output-dir", default="build/memory-tests")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if any(mb < 64 for mb in args.memory) or len(set(args.memory)) != len(args.memory):
        parser.error("RAM sizes must be unique and at least 64 MB")
    if args.timeout < 60:
        parser.error("--timeout must be at least 60 seconds")
    if not Path("/proc/self/status").exists():
        parser.error("Run the QEMU memory matrix on Linux so its RAM safety monitor is available")
    root = Path(__file__).resolve().parents[1]
    output = (root / args.output_dir).resolve()
    folder = output / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True, exist_ok=False)
    report = {"result": "running", "resolution": args.resolution, "planned_memory_mb": args.memory,
              "scope": "All usable frame allocations and recovery; sampled physical reads/writes, not an exhaustive byte test",
              "memory_backend": "memory-backend-ram,reserve=off,prealloc=off", "cases": []}

    def save():
        for parent in [folder, output]:
            pending = parent / "result.pending"
            pending.write_text(json.dumps(report, indent=2) + "\n")
            pending.replace(parent / "result.json")

    save()
    try:
        config = folder / "limine.conf"
        config.write_text("timeout: 0\n\n/ShawnOS memory capacity test\n    protocol: limine\n"
                          f"    path: boot():/boot/myos.elf\n    resolution: {args.resolution}x32\n"
                          "    cmdline: selftest=memory\n")
        iso = folder / "memory-test.iso"
        subprocess.run(["make", "iso", f"ISO={iso}", f"LIMINE_CONFIG={config}"], cwd=root, check=True, timeout=120)
        report["iso_sha256"] = hashlib.sha256(iso.read_bytes()).hexdigest()
        for index, mb in enumerate(args.memory, 1):
            case_folder = folder / f"{mb}M"
            case_folder.mkdir()
            command = ["python3", "-u", "scripts/desktop-test.py", str(iso), "--expect-size", args.resolution,
                       "--memory", f"{mb}M", "--memory-no-reserve", "--require-capacity-test",
                       "--output-dir", str(case_folder)]
            print(f"[{index}/{len(args.memory)}] Testing all usable frame allocations at {mb} MB, {args.resolution}", flush=True)
            began, peak = time.monotonic(), 0
            environment = dict(os.environ, MYOS_SMOKE_TIMEOUT="180", MYOS_DESKTOP_TIMEOUT=str(args.timeout - 10))
            with (case_folder / "test-output.log").open("w") as log:
                process = subprocess.Popen(command, cwd=root, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                           start_new_session=True)
                try:
                    while process.poll() is None:
                        peak = max(peak, process_rss(process.pid))
                        if peak > 1536 * 1024:
                            raise RuntimeError("Test safety limit exceeded: above 1.5 GB actual process-tree RAM")
                        if time.monotonic() - began > args.timeout:
                            raise TimeoutError(f"{mb} MB case timed out")
                        time.sleep(0.5)
                    if process.returncode:
                        raise RuntimeError((case_folder / "test-output.log").read_text()[-5000:])
                finally:
                    stop_process(process)
            result = json.loads((case_folder / "result.json").read_text())
            assert result["result"] == "passed" and result["iso_sha256"] == report["iso_sha256"]
            detected = result["memory_reports"]["Detected RAM"]
            assert 0 <= mb * (1 << 20) - detected["bytes"] < 2 * (1 << 20), detected
            assert result["allocator"]["capacity_test"] is not None
            result.update(assigned_mb=mb, peak_process_tree_rss_kb=peak,
                          elapsed_seconds=round(time.monotonic() - began, 2), evidence=str(case_folder), command=command)
            report["cases"].append(result)
            save()
            memory = result["memory_reports"]
            print(f"[{index}/{len(args.memory)}] PASS: detected {detected['text']}, managed {memory['Managed RAM']['text']}, "
                  f"free {memory['Free RAM']['text']}; all pages allocated and recovered", flush=True)
        report["result"] = "passed"
    except BaseException as error:
        report["result"] = "failed"
        report["error"] = str(error)
        save()
        raise
    save()
    print(f"MEMORY CAPACITY MATRIX PASSED: {len(report['cases'])} cases. Evidence: {folder}", flush=True)


if __name__ == "__main__":
    main()
