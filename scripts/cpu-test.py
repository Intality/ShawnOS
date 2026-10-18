#!/usr/bin/env python3
"""Boot one unchanged ISO with multiple x86-64 CPUs and firmware reports."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("iso", nargs="?", default="build/myos-x86_64.iso")
    parser.add_argument("--output-dir", default="build/cpu-tests")
    parser.add_argument("--case", help="Run one named case instead of the full matrix")
    parser.add_argument("--resume", action="store_true", help="Reuse matching passed evidence for this exact ISO")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    config = json.loads((repo / "tests/cpu-reports.json").read_text())
    if config["schema"] != 1:
        parser.error("Unsupported CPU test schema")
    cases = [case for case in config["cases"] if not args.case or case["name"] == args.case]
    if not cases:
        parser.error("Unknown CPU case")
    iso = Path(args.iso).resolve()
    digest = hashlib.sha256(iso.read_bytes()).hexdigest()
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    previous = {}
    report_path = output / "result.json"
    if args.resume and report_path.exists():
        saved = json.loads(report_path.read_text())
        if saved["iso_sha256"] != digest:
            parser.error("Cannot resume evidence from a different ISO")
        previous = {case["name"]: case for case in saved["cases"]}
    results = []

    def save(status):
        report_path.write_text(json.dumps({"iso_sha256": digest, "status": status, "cases": results}, indent=2) + "\n")

    save("running")
    for case in cases:
        folder = output / case["name"]
        prior = previous.get(case["name"], {})
        if prior.get("result") == "passed" and all(prior.get(key) == value for key, value in case.items()):
            evidence = json.loads((folder / "result.json").read_text())
            if (evidence["iso_sha256"] == digest and evidence["result"] == "passed" and
                    evidence["cpu_speed"] == case["expected"] and evidence["cpu_speed_label"] == case["label"] and
                    evidence["cpu_model"] == case["cpu"]):
                results.append(prior)
                save("running")
                print(f"REUSE VERIFIED CASE {case['name']}: unchanged ISO and matching passed evidence", flush=True)
                continue
        print(f"CPU CASE {case['name']}: {case['label']} = {case['expected']}", flush=True)
        try:
            subprocess.run([
                sys.executable, str(repo / "scripts/desktop-test.py"), str(iso),
                "--output-dir", str(folder), "--memory", "64M", "--cpu", case["cpu"],
                "--firmware-cpu-mhz", str(case["firmware_mhz"]), "--smbios-entry-point", case["entry"],
                "--expect-cpu-speed", case["expected"], "--expect-cpu-label", case["label"],
            ], check=True, cwd=repo)
        except subprocess.CalledProcessError:
            results.append({**case, "result": "failed", "actual": None})
            save("failed")
            raise
        report = json.loads((folder / "result.json").read_text())
        if hashlib.sha256(iso.read_bytes()).hexdigest() != digest or report["iso_sha256"] != digest:
            raise RuntimeError("ISO changed during the CPU test matrix")
        results.append({**case, "result": report["result"], "actual": report["cpu_speed"]})
        save("running")
    save("passed")
    print(f"CPU compatibility matrix passed: {len(results)} cases", flush=True)


if __name__ == "__main__":
    main()
