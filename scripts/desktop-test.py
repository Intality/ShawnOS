#!/usr/bin/env python3
"""Exercise real PS/2 input and framebuffer output through QEMU's QMP API."""
import argparse
from decimal import Decimal, ROUND_HALF_UP
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import time


class QMP:
    def __init__(self, path, process):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(10)
        deadline = time.monotonic() + 15
        while True:
            try:
                self.socket.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if process.poll() is not None or time.monotonic() > deadline:
                    self.socket.close()
                    raise RuntimeError("QEMU did not open its QMP socket")
                time.sleep(0.1)
        self.stream = self.socket.makefile("rwb")
        greeting = json.loads(self.stream.readline())
        if "QMP" not in greeting:
            raise RuntimeError("Invalid QMP greeting")
        self.sequence = 0
        self.execute("qmp_capabilities")

    def execute(self, command, **arguments):
        self.sequence += 1
        request = {"execute": command, "id": self.sequence}
        if arguments:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode() + b"\n")
        self.stream.flush()
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QEMU closed QMP unexpectedly")
            response = json.loads(line)
            if response.get("id") != self.sequence:
                continue
            if "error" in response:
                raise RuntimeError(response["error"])
            return response.get("return")

    def close(self):
        self.stream.close()
        self.socket.close()


def read_ppm(path):
    raw = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", raw)
    if not match:
        raise RuntimeError("Unexpected QEMU screenshot format")
    width, height = map(int, match.groups())
    pixels = raw[match.end():]
    if len(pixels) != width * height * 3:
        raise RuntimeError("Incomplete screenshot")
    return width, height, pixels


def memory_text(byte_count):
    unit = (1 << 30) if byte_count >= (1 << 30) else (1 << 20)
    value = (Decimal(byte_count) / Decimal(unit)).quantize(Decimal("0.01"), rounding=ROUND_HALF_UP)
    return f"{value} {'GB' if unit == (1 << 30) else 'MB'}"


def uptime_text(seconds):
    days, seconds = divmod(seconds, 86400)
    hours, seconds = divmod(seconds, 3600)
    minutes, seconds = divmod(seconds, 60)
    units = list(zip((days, hours, minutes, seconds), "dhms"))
    first = next((i for i, (value, _) in enumerate(units) if value), 3)
    return " ".join(f"{value}{unit}" for value, unit in units[first:])


def assert_text_pixels(pixels, width, x, y, text, available=260,
                       ink_color=bytes.fromhex("243531"), paper_color=bytes.fromhex("f5f8f6"),
                       scale=None):
    # Exact 5x7 fixtures verify the visible value, not only its serial report.
    glyphs = {
        "0": [14,17,19,21,25,17,14], "1": [4,12,4,4,4,4,14],
        "2": [14,17,1,2,4,8,31], "3": [30,1,1,14,1,1,30],
        "4": [2,6,10,18,31,2,2], "5": [31,16,16,30,1,1,30],
        "6": [6,8,16,30,17,17,14], "7": [31,1,2,4,8,8,8],
        "8": [14,17,17,14,17,17,14], "9": [14,17,17,15,1,2,12],
        "M": [17,27,21,21,17,17,17], "B": [30,17,17,30,17,17,30],
        "G": [14,17,16,23,17,17,15], ".": [0,0,0,0,0,0,4], " ": [0]*7,
        "A": [14,17,17,31,17,17,17], "D": [30,17,17,17,17,17,30],
        "C": [14,17,16,16,16,17,14], "H": [17,17,17,31,17,17,17],
        "F": [31,16,16,30,16,16,16], "R": [30,17,17,30,20,18,17],
        "P": [30,17,17,30,16,16,16], "V": [17,17,17,17,17,10,4],
        "U": [17,17,17,17,17,17,14], ":": [0,0,4,0,0,4,0],
        ">": [0,8,4,2,4,8,0],
        "a": [0,0,14,1,15,17,15], "b": [16,16,30,17,17,17,30],
        "c": [0,0,14,16,16,17,14], "d": [1,1,15,17,17,17,15],
        "e": [0,0,14,17,31,16,14], "g": [0,15,17,17,15,1,14],
        "h": [16,16,30,17,17,17,17], "i": [4,0,12,4,4,4,14],
        "l": [12,4,4,4,4,4,14], "n": [0,0,30,17,17,17,17],
        "m": [0,0,26,21,21,21,21], "p": [0,0,30,17,30,16,16],
        "r": [0,0,22,25,16,16,16], "s": [0,0,15,16,14,1,30],
        "t": [8,8,28,8,8,9,6], "o": [0,0,14,17,17,17,14],
        "v": [0,0,17,17,17,10,4], "z": [0,0,31,2,4,8,31],
    }
    if scale is None:
        scale = 1 if len(text) * 12 > available else 2
    assert len(text) * 6 * scale <= available, f"Text does not fit: {text}"
    for index, char in enumerate(text):
        for row in range(7):
            for col in range(6):
                ink = col < 5 and glyphs[char][row] & (1 << (4 - col))
                expected = ink_color if ink else paper_color
                for dy in range(scale):
                    for dx in range(scale):
                        start = ((y + row * scale + dy) * width + x + (index * 6 + col) * scale + dx) * 3
                        assert pixels[start:start + 3] == expected, f"Wrong visible text: {text}"


def assert_terminal_rows(pixels, width, height, lines, *, ends_at_prompt=False):
    w, h = min(580, width - 120), min(350, height - 100)
    x = max(0, min(width - w - 4, (width - w) // 2 + 32)) + 16
    y = max(32, min(height - 48 - h - 4, (height - 48 - h) // 2 + 12)) + 50
    lines = [line + " " for line in lines]
    available = w - 32
    assert all(len(line) * 12 <= available for line in lines), "Terminal text does not fit"
    # Prior commands can scroll out of view; find the expected consecutive rows.
    for row in range((h - 72) // 20 - len(lines) + 1):
        try:
            for offset, line in enumerate(lines):
                assert_text_pixels(pixels, width, x, y + (row + offset) * 20, line, available,
                                   ink_color=bytes.fromhex("d9e9df"), paper_color=bytes.fromhex("1d2926"))
            if ends_at_prompt:
                assert_text_pixels(pixels, width, x, y + (row + len(lines)) * 20, "> ", available,
                                   ink_color=bytes.fromhex("9be2c1"), paper_color=bytes.fromhex("1d2926"))
        except AssertionError:
            continue
        return
    raise AssertionError(f"Missing visible Terminal rows: {lines}")


def visible_uptime(pixels, width, x, y, first, last):
    for seconds in range(first, last + 1):
        try:
            assert_text_pixels(pixels, width, x, y, uptime_text(seconds) + " ")
        except AssertionError:
            continue
        return seconds
    raise AssertionError(f"About did not display formatted uptime between {first}s and {last}s")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("iso", nargs="?", default="build/myos-x86_64.iso")
    parser.add_argument("--output-dir", default="build/desktop-test")
    parser.add_argument("--expect-size", default="1024x768")
    parser.add_argument("--memory", default="256M")
    parser.add_argument("--memory-no-reserve", action="store_true",
                        help="Use on-demand QEMU test RAM without reserving host swap")
    parser.add_argument("--require-capacity-test", action="store_true",
                        help="Require an ISO booted with selftest=memory")
    parser.add_argument("--cpu", help="QEMU CPU model and properties for reporting tests")
    parser.add_argument("--expect-cpu-base", "--expect-cpu-speed", dest="expect_cpu_speed",
                        help="Expected formatted CPU speed, or Unavailable")
    parser.add_argument("--expect-cpu-label", choices=["CPU base speed", "CPU boot speed"])
    parser.add_argument("--firmware-cpu-mhz", type=int, help="Synthetic SMBIOS startup speed, 0 means unknown")
    parser.add_argument("--smbios-entry-point", choices=["32", "64"], default="64")
    args = parser.parse_args()
    if args.firmware_cpu_mhz is not None and not 0 <= args.firmware_cpu_mhz <= 65535:
        parser.error("--firmware-cpu-mhz must be between 0 and 65535")
    total_timeout = int(os.environ.get("MYOS_DESKTOP_TIMEOUT", "180"))
    if total_timeout <= 0:
        parser.error("MYOS_DESKTOP_TIMEOUT must be a positive number of seconds")

    def timed_out(_signal, _frame):
        raise TimeoutError("Desktop interaction test exceeded its total time limit")

    signal.signal(signal.SIGALRM, timed_out)
    signal.alarm(total_timeout)
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    iso = Path(args.iso).resolve()
    serial = output / "serial.log"
    serial.write_text("")
    # AF_UNIX has a short path limit; keep only the transient socket under /tmp.
    import tempfile
    with tempfile.TemporaryDirectory(prefix="myos-qmp-") as temp, (output / "qemu.log").open("w") as error_log:
        process = subprocess.Popen([
            "qemu-system-x86_64", "-M", "q35", "-smp", "1", "-m", args.memory,
            *(["-object", f"memory-backend-ram,id=myos-test-ram,size={args.memory},reserve=off,prealloc=off",
               "-machine", "memory-backend=myos-test-ram"] if args.memory_no_reserve else []),
            *(["-cpu", args.cpu] if args.cpu else []), "-cdrom", str(iso),
            "-machine", f"smbios-entry-point-type={args.smbios_entry_point}",
            *(["-smbios", f"type=4,current-speed={args.firmware_cpu_mhz},max-speed=5000"]
              if args.firmware_cpu_mhz is not None else []),
            "-boot", "d", "-display", "none", "-serial", f"file:{serial}",
            "-qmp", f"unix:{temp}/qmp,server=on,wait=off", "-no-reboot", "-no-shutdown",
        ], stdout=subprocess.DEVNULL, stderr=error_log)
        qmp = None
        try:
            qmp = QMP(Path(temp) / "qmp", process)

            def log():
                return serial.read_text(errors="replace")

            def wait_marker(marker, count=1):
                deadline = time.monotonic() + float(os.environ.get("MYOS_SMOKE_TIMEOUT", "60"))
                while log().count(marker) < count:
                    if process.poll() is not None or "MYOS: panic halted" in log() or time.monotonic() > deadline:
                        raise RuntimeError(f"Missing marker: {marker}\n{log()}")
                    time.sleep(0.1)

            def screenshot(name):
                time.sleep(0.15)
                path = output / f"{name}.ppm"
                qmp.execute("screendump", filename=str(path))
                return read_ppm(path)

            def key(*codes):
                qmp.execute("send-key", keys=[{"type": "qcode", "data": code} for code in codes], **{"hold-time": 40})
                time.sleep(0.16)

            def type_text(text):
                punctuation = {" ": "spc", "-": "minus", ".": "dot", "\n": "ret"}
                for char in text:
                    if char == "!":
                        key("shift", "1")
                    elif char.isupper():
                        key("shift", char.lower())
                    else:
                        key(punctuation.get(char, char))

            wait_marker("MYOS: boot ok v0.4.0")
            wait_marker("MYOS: GDT IDT TSS ready")
            wait_marker("MYOS: memory map ready")
            wait_marker("MYOS: frame allocator self-test passed")
            stats = re.search(r"MYOS: frame allocator ready managed=(\d+) metadata=(\d+) highest=(\d+)", log())
            assert stats, "Missing allocator capacity diagnostics"
            managed_frames, metadata_frames, highest = map(int, stats.groups())
            assert 0 < metadata_frames < managed_frames
            high = re.search(r"MYOS: high memory write/read passed first=(\d+) last=(\d+)", log())
            high_memory = None
            if highest > (1 << 32):
                assert high, "No real high-memory read/write test"
                high_first, high_last = map(int, high.groups())
                assert (1 << 32) <= high_first <= high_last == highest - 4096
                high_memory = {"first": high_first, "last": high_last}
            capacity = re.search(r"MYOS: memory capacity passed allocated=(\d+) high=(\d+) released=(\d+)", log())
            capacity_result = None
            if args.require_capacity_test:
                assert capacity, "Missing full-capacity exhaustion/recovery test"
            if capacity:
                allocated, high_count, released = map(int, capacity.groups())
                assert allocated == released == managed_frames - metadata_frames
                assert (high_count > 0) == (highest > (1 << 32))
                capacity_result = {"allocated": allocated, "high": high_count, "released": released}
            wait_marker("MYOS: PIT ready 100 Hz")
            wait_marker("MYOS: timer ticked")
            wait_marker("MYOS: desktop ready")
            wait_marker("MYOS: keyboard ready")
            wait_marker("MYOS: mouse ready")
            width, height, pixels = screenshot("desktop")
            print(f"Desktop booted at {width}x{height}; checking mouse and keyboard.", flush=True)
            expected = tuple(map(int, args.expect_size.split("x")))
            assert (width, height) == expected, (width, height, expected)

            def pixel(image, x, y):
                start = (y * width + x) * 3
                return image[start:start + 3]

            assert pixel(pixels, 0, 0) == bytes.fromhex("243531"), "Missing top panel"
            assert pixel(pixels, width - 1, height - 1) == bytes.fromhex("f5f8f6"), "Missing taskbar"
            assert len({pixels[i:i + 3] for i in range(0, len(pixels), 3)}) >= 15, "Blank framebuffer"
            position = [width * 3 // 4, height * 3 // 4]

            def region(image, x, y, w, h):
                return b"".join(image[((y + row) * width + x) * 3:((y + row) * width + x + w) * 3]
                                for row in range(h))

            about_x = (width - 420) // 2
            about_y = max(32, min(height - 48 - 300 - 4, (height - 48 - 300) // 2 + 12))

            def move_to(x, y):
                while position != [x, y]:
                    dx = max(-64, min(64, x - position[0]))
                    dy = max(-64, min(64, y - position[1]))
                    qmp.execute("input-send-event", events=[
                        {"type": "rel", "data": {"axis": "x", "value": dx}},
                        {"type": "rel", "data": {"axis": "y", "value": dy}},
                    ])
                    position[0] += dx
                    position[1] += dy
                    time.sleep(0.12)

            def button(down):
                qmp.execute("input-send-event", events=[{"type": "btn", "data": {"button": "left", "down": down}}])
                time.sleep(0.2)

            def click(x, y):
                move_to(x, y)
                button(True)
                button(False)

            move_to(64, height - 24)
            no_tooltip = screenshot("taskbar-neutral")[2]
            move_to(74, height - 24)
            tooltip = screenshot("taskbar-hover")[2]
            assert pixel(no_tooltip, 44, height - 72) != pixel(tooltip, 44, height - 72), "Taskbar hover did not update"
            assert pixel(tooltip, 44, height - 72) == bytes.fromhex("243531"), "Missing taskbar tooltip"
            click(64, 85)
            wait_marker("MYOS: open Terminal")
            print("Mouse launcher passed; checking Terminal.", flush=True)
            type_text("help\n")
            wait_marker("MYOS: command help")
            before_echo = screenshot("terminal-help")[2]
            type_text("echo desktop-ready\n")
            wait_marker("MYOS: command echo")
            assert screenshot("terminal")[2] != before_echo, "Terminal output did not render"
            type_text("cpu\n")
            wait_marker("MYOS: command cpu")
            cpu_reports = re.findall(r"^MYOS: (CPU (?:base|boot) speed): ([^\r\n]+)", log(), re.MULTILINE)
            assert len(cpu_reports) == 1, f"Missing or ambiguous CPU report: {log()}"
            cpu_label, cpu_speed = cpu_reports[0]
            assert re.fullmatch(r"Unavailable|[1-9]\d{0,2} MHz|[1-9]\d*\.\d{2} GHz", cpu_speed), cpu_speed
            if args.expect_cpu_speed is not None:
                assert cpu_speed == args.expect_cpu_speed, (cpu_speed, args.expect_cpu_speed)
            if args.expect_cpu_label is not None:
                assert cpu_label == args.expect_cpu_label, (cpu_label, args.expect_cpu_label)
            assert re.search(r"(?m)^MYOS: TSC clock: (?:~[1-9]\d* MHz|unavailable)\r?$", log()), log()
            assert_terminal_rows(screenshot("cpu")[2], width, height, [f"{cpu_label}: {cpu_speed}"],
                                 ends_at_prompt=True)
            type_text("mem\n")
            wait_marker("MYOS: command mem")
            memory_reports = re.findall(r"^MYOS: memory (Detected RAM|Usable RAM|Managed RAM|Free RAM) bytes=(\d+) value=([^\r\n]+)",
                                        log(), re.MULTILINE)
            assert [entry[0] for entry in memory_reports] == ["Detected RAM", "Usable RAM", "Managed RAM", "Free RAM"], log()
            memory = {label: {"bytes": int(raw), "text": text} for label, raw, text in memory_reports}
            for report in memory.values():
                assert report["text"] == memory_text(report["bytes"]), report
            assert memory["Detected RAM"]["bytes"] >= memory["Usable RAM"]["bytes"] >= memory["Managed RAM"]["bytes"] >= memory["Free RAM"]["bytes"] > 0
            assert memory["Managed RAM"]["bytes"] == managed_frames * 4096
            assert memory["Usable RAM"]["bytes"] - managed_frames * 4096 < 4096 * 4096, "Usable RAM was left unmanaged"
            assert memory["Free RAM"]["bytes"] <= (managed_frames - metadata_frames) * 4096
            memory_lines = [f"{label}: {item['text']}" for label, item in memory.items()]
            assert_terminal_rows(screenshot("memory-terminal")[2], width, height, memory_lines)
            key("f3")
            about_memory = screenshot("memory-about")[2]
            muted = bytes.fromhex("61736d")
            assert_text_pixels(about_memory, width, about_x + 92, about_y + 94, "Version 0.4.0 ", ink_color=muted)
            for label, offset in [(cpu_label, 174), ("Uptime", 246)]:
                assert_text_pixels(about_memory, width, about_x + 24, about_y + offset, label + " ",
                                   ink_color=muted, scale=1)
            assert_text_pixels(about_memory, width, about_x + 136, about_y + 174, cpu_speed + " ")
            for label, offset in [("Detected RAM", 198), ("Free RAM", 222)]:
                assert_text_pixels(about_memory, width, about_x + 136, about_y + offset, memory[label]["text"])
            key("f1")
            type_text("uptime\n")
            wait_marker("MYOS: uptime seconds=")
            key("f3")
            uptime_before = screenshot("uptime-before")[2]
            time.sleep(1.2)
            uptime_after = screenshot("uptime-after")[2]
            key("f1")
            type_text("uptime\n")
            wait_marker("MYOS: uptime seconds=", 2)
            uptime_samples = re.findall(r"MYOS: uptime seconds=(\d+) ticks=(\d+) idle=(\d+)", log())
            assert len(uptime_samples) == 2, log()
            first, second = [tuple(map(int, sample)) for sample in uptime_samples]
            assert second[0] > first[0] and second[1] > first[1] and second[2] > first[2] > 0, "Timer or idle did not progress"
            assert_terminal_rows(screenshot("system-info")[2], width, height, [f"Uptime: {uptime_text(second[0])}"],
                                 ends_at_prompt=True)
            about_before = visible_uptime(uptime_before, width, about_x + 136, about_y + 246, first[0], second[0])
            about_after = visible_uptime(uptime_after, width, about_x + 136, about_y + 246, first[0], second[0])
            assert about_after > about_before, "About uptime did not refresh"
            key("f2")
            wait_marker("MYOS: open Notes")
            print("Terminal passed; checking Notes and window controls.", flush=True)
            blank_note = screenshot("notes-blank")[2]
            note_w, note_h = min(460, width - 120), min(360, height - 100)
            note_x = max(0, min(width - note_w - 4, (width - note_w) // 2 - 24))
            note_y = max(32, min(height - 48 - note_h - 4, (height - 48 - note_h) // 2 + 12))
            type_text("Hello Desktop\nline 2!")
            wait_marker("MYOS: note edited")
            written_note = screenshot("notes")[2]
            assert sum(a != b for a, b in zip(region(blank_note, note_x, note_y, note_w, note_h),
                region(written_note, note_x, note_y, note_w, note_h))) > 250, "Notes did not render typing"
            key("backspace")
            edited_note = screenshot("notes-edited")[2]
            assert region(edited_note, note_x, note_y, note_w, note_h) != region(
                written_note, note_x, note_y, note_w, note_h), "Backspace did not edit Notes"
            key("esc")
            key("f2")
            assert region(screenshot("notes-restored")[2], note_x, note_y, note_w, note_h) == region(
                edited_note, note_x, note_y, note_w, note_h), "Closing Notes lost session text"

            note_w, note_h = min(460, width - 120), min(360, height - 100)
            note_x = max(0, min(width - note_w - 4, (width - note_w) // 2 - 24))
            note_y = max(32, min(height - 48 - note_h - 4, (height - 48 - note_h) // 2 + 12))
            minimizations = log().count("MYOS: window minimized")
            click(note_x + note_w - 52, note_y + 18)
            wait_marker("MYOS: window minimized", minimizations + 1)
            screenshot("minimized")
            opens = log().count("MYOS: open Notes")
            click(136, height - 24)
            wait_marker("MYOS: open Notes", opens + 1)
            move_to(note_x + 150, note_y + 18)
            button(True)
            move_to(note_x + 178, note_y + 34)
            button(False)
            wait_marker("MYOS: window moved")
            screenshot("dragged")
            closes = log().count("MYOS: window closed")
            click(note_x + 28 + note_w - 20, note_y + 16 + 18)
            wait_marker("MYOS: window closed", closes + 1)

            click(32, height - 24)
            wait_marker("MYOS: launcher opened")
            print("Window controls passed; checking launcher and wallpaper.", flush=True)
            screenshot("launcher")
            click(110, height - 48 - 256 + 48 + 3 * 46 + 21)
            wait_marker("MYOS: open Appearance")
            style_x = (width - 400) // 2
            style_y = max(32, min(height - 48 - 280 - 4, (height - 48 - 280) // 2 + 12))
            sky_before = screenshot("appearance")[2]
            click(style_x + 24 + 184 + 84, style_y + 140)
            wait_marker("MYOS: wallpaper changed")
            sky_after = screenshot("dusk")[2]
            assert pixel(sky_before, width - 4, 50) != pixel(sky_after, width - 4, 50), "Wallpaper did not change"
            qmp.execute("quit")
            process.wait(timeout=10)
            result = {"version": "0.4.0", "resolution": args.expect_size, "memory": args.memory, "cpus": 1, "result": "passed",
                      "checks": ["desktop pixels", "taskbar hover", "PS/2 mouse launch", "terminal commands", "typing",
                                 "backspace", "session note retention", "minimize and restore", "drag", "close", "launcher", "wallpaper",
                                 "frame allocator boot self-test", "live uptime", "formatted About and Terminal uptime",
                                 "About system information", "timer details in serial only",
                                 "1024-based memory command",
                                 "Terminal memory pixels", "About memory pixels", "timer and HLT wake progress"],
                      "memory_reports": memory,
                      "uptime": {"about_seconds": [about_before, about_after], "terminal_seconds": second[0],
                                 "terminal_text": uptime_text(second[0])},
                      "allocator": {"managed_frames": managed_frames, "metadata_frames": metadata_frames,
                                    "highest_address": highest, "high_memory_write_read": high_memory,
                                    "capacity_test": capacity_result},
                      "memory_no_reserve": args.memory_no_reserve,
                      "cpu_model": args.cpu, "cpu_speed": cpu_speed, "cpu_speed_label": cpu_label,
                      "firmware_cpu_mhz": args.firmware_cpu_mhz, "smbios_entry_point": args.smbios_entry_point,
                      "iso_sha256": hashlib.sha256(iso.read_bytes()).hexdigest()}
            (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, indent=2))
        except BaseException:
            if qmp is not None and process.poll() is None:
                try:
                    qmp.execute("screendump", filename=str(output / "failure.ppm"))
                    for info in ["registers", "pic", "lapic", "irq"]:
                        diagnostic = qmp.execute("human-monitor-command", **{"command-line": f"info {info}"})
                        (output / f"failure-{info}.txt").write_text(diagnostic)
                except Exception:
                    pass
            print(log() if qmp is not None else "QMP connection failed")
            raise
        finally:
            signal.alarm(0)
            if qmp is not None:
                qmp.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
