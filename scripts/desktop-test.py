#!/usr/bin/env python3
"""Exercise real PS/2 input and framebuffer output through QEMU's QMP API."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("iso", nargs="?", default="build/myos-x86_64.iso")
    parser.add_argument("--output-dir", default="build/desktop-test")
    parser.add_argument("--expect-size", default="1024x768")
    args = parser.parse_args()
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    iso = Path(args.iso).resolve()
    serial = output / "serial.log"
    serial.write_text("")
    # AF_UNIX has a short path limit; keep only the transient socket under /tmp.
    import tempfile
    with tempfile.TemporaryDirectory(prefix="myos-qmp-") as temp, (output / "qemu.log").open("w") as error_log:
        process = subprocess.Popen([
            "qemu-system-x86_64", "-M", "q35", "-m", "256M", "-cdrom", str(iso),
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
                    if process.poll() is not None or time.monotonic() > deadline:
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

            wait_marker("MYOS: boot ok v0.2.0")
            wait_marker("MYOS: desktop ready")
            wait_marker("MYOS: keyboard ready")
            wait_marker("MYOS: mouse ready")
            width, height, pixels = screenshot("desktop")
            expected = tuple(map(int, args.expect_size.split("x")))
            assert (width, height) == expected, (width, height, expected)

            def pixel(image, x, y):
                start = (y * width + x) * 3
                return image[start:start + 3]

            assert pixel(pixels, 0, 0) == bytes.fromhex("243531"), "Missing top panel"
            assert pixel(pixels, width - 1, height - 1) == bytes.fromhex("f5f8f6"), "Missing taskbar"
            assert len({pixels[i:i + 3] for i in range(0, len(pixels), 3)}) >= 15, "Blank framebuffer"
            position = [width * 3 // 4, height * 3 // 4]

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
            type_text("help\n")
            wait_marker("MYOS: command help")
            before_echo = screenshot("terminal-help")[2]
            type_text("echo desktop-ready\n")
            wait_marker("MYOS: command echo")
            assert screenshot("terminal")[2] != before_echo, "Terminal output did not render"
            key("f2")
            wait_marker("MYOS: open Notes")
            blank_note = screenshot("notes-blank")[2]
            type_text("Hello Desktop\nline 2!")
            wait_marker("MYOS: note edited")
            written_note = screenshot("notes")[2]
            assert sum(a != b for a, b in zip(blank_note, written_note)) > 250, "Notes did not render typing"
            key("backspace")
            edited_note = screenshot("notes-edited")[2]
            assert edited_note != written_note, "Backspace did not edit Notes"
            key("esc")
            key("f2")
            assert screenshot("notes-restored")[2] == edited_note, "Closing Notes lost session text"

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
            result = {"version": "0.2.0", "resolution": args.expect_size, "result": "passed",
                      "checks": ["desktop pixels", "taskbar hover", "PS/2 mouse launch", "terminal commands", "typing",
                                 "backspace", "session note retention", "minimize and restore", "drag", "close", "launcher", "wallpaper"],
                      "iso_sha256": hashlib.sha256(iso.read_bytes()).hexdigest()}
            (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, indent=2))
        except BaseException:
            if qmp is not None and process.poll() is None:
                try:
                    qmp.execute("screendump", filename=str(output / "failure.ppm"))
                except Exception:
                    pass
            print(log() if qmp is not None else "QMP connection failed")
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


if __name__ == "__main__":
    main()
