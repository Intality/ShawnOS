# ShawnOS

ShawnOS is a small independent 64-bit x86-64 operating system written in
freestanding C++. It is not Debian, not Linux, and not a repackaged distro. The
v0.2.0 kernel boots through Limine into an interactive framebuffer desktop.

Repository: [Intality/ShawnOS](https://github.com/Intality/ShawnOS). Kernel and
artifact filenames retain the original temporary MyOS name for compatibility.

## What Is Included

- Limine boot protocol setup.
- Higher-half x86-64 ELF kernel.
- Freestanding C++20 with no standard library, exceptions, or RTTI.
- A tiny assembly file for `hlt`, `inb`, and `outb`.
- Desktop with an original raster mountain wallpaper, launcher, and taskbar.
- Draggable windows with focus, close, minimize, and restore controls.
- PS/2 mouse and US-QWERTY keyboard input, including Shift and Caps Lock.
- Terminal with `help`, `clear`, `version`, `echo`, `about`, `notes`, and `wallpaper`.
- Notes editor with typing, newline, backspace, wrapping, and session retention.
- About and Appearance apps; Alpine and Dusk wallpaper choices.
- Serial logging for test automation.
- ISO build, QEMU run, QEMU debug, and QEMU smoke-test targets.
- Unit tests and QMP-driven desktop interaction tests at 1024x768 and 640x480.
- GitHub Actions CI that builds the ISO and runs the tests.
- Release notes and a small incremental roadmap.

## Prerequisites

Use Ubuntu in VirtualBox, WSL2, or a native Linux installation. Install:

```sh
sudo apt-get update
sudo apt-get install -y git build-essential clang lld curl xorriso qemu-system-x86 python3
```

You can also use a GCC cross compiler:

```sh
make TOOLCHAIN=gnu TOOLCHAIN_PREFIX=x86_64-elf-
```

## Commands

Get the source:

```sh
git clone https://github.com/Intality/ShawnOS.git
cd ShawnOS
```

Check local tools:

```sh
make check-tools
```

Build the bootable ISO:

```sh
make TOOLCHAIN=llvm
```

Run it in QEMU:

```sh
make run TOOLCHAIN=llvm
```

Run with a debugger waiting on port `1234`:

```sh
make debug TOOLCHAIN=llvm
```

Run the automated desktop boot smoke test:

```sh
make smoke TOOLCHAIN=llvm
```

Run unit tests and automated mouse/keyboard desktop tests:

```sh
make test TOOLCHAIN=llvm
```

Test the smaller display mode:

```sh
make iso TOOLCHAIN=llvm ISO=build/myos-640.iso LIMINE_CONFIG=tests/limine-640.conf
python3 scripts/desktop-test.py build/myos-640.iso --expect-size 640x480 --output-dir build/desktop-test-640
```

The desktop test records screenshots, serial output, and a JSON result under
`build/desktop-test/`. It drives the virtual hardware, not a simulated web UI.

The test allows up to 60 seconds for boot and stops as soon as the serial success
marker appears. For slower machines, use `MYOS_SMOKE_TIMEOUT=120 make smoke`.
QEMU uses software emulation by default, so running it inside Ubuntu in VirtualBox
does not require nested virtualization.

The ISO is written to:

```text
build/myos-x86_64.iso
```

## Code Tour

- `kernel/src/boot.cpp` defines the Limine requests and the `kmain` kernel entry.
- `kernel/src/graphics.cpp` clips drawing, renders the built-in bitmap font,
  handles RGB masks and pitch, and copies pixels to Limine's framebuffer.
- `kernel/src/desktop.cpp` owns the desktop, window stacking, apps, input loop,
  wallpaper, and cursor. Fixed backing buffers avoid needing an allocator yet.
- `kernel/src/arch/x86_64/ps2.cpp` initializes the PS/2 controller and polls input.
- `kernel/src/input_decoder.cpp` decodes key scan codes and mouse packets;
  the exact same decoder is covered by host unit tests.
- `kernel/src/serial.cpp` writes boot status messages to COM1 for QEMU tests.
- `kernel/src/arch/x86_64/cpu.S` provides the port I/O and fatal halt routines.
- `kernel/src/lib/memory.cpp` supplies the memory routines compilers may emit.
- `kernel/src/lib/abi.cpp` supplies minimal C++ ABI hooks for freestanding code.
- `kernel/linker/x86_64.ld` places the kernel in the higher half and preserves
  the Limine request section.

## Desktop Controls

Click a desktop icon or taskbar icon to open an app. The four-square button opens
the launcher. Drag a window's title bar to move it. The minus button minimizes;
click its taskbar icon to restore it. The cross closes the window.

F1 opens Terminal, F2 Notes, F3 About, and F4 Appearance. Tab cycles visible
windows. Escape dismisses the launcher or closes the focused window. Notes is an
append-only editor with Backspace and Enter; text survives closing the window,
but not rebooting. QEMU's default mouse capture can be released with Ctrl+Alt.

## Current Limits

This is a real early kernel desktop, not a complete general-purpose OS. Apps all
run inside one kernel loop, not as isolated processes. There is no disk-backed
filesystem, USB input driver, networking, sound, or installed-program support.
Terminal commands are built-ins, not Linux programs. Notes has a 2047-character
RAM limit. Desktop resolutions supported are 640x480 through 1920x1200, RGB32;
the normal ISO requests 1024x768.

Input uses bounded PS/2 controller polling with interrupts masked. Without an
IDT and timer yet, idle uses `pause`, not `hlt`, and may keep a CPU core busy.
The next kernel work is exception handling, interrupts, memory allocation, and
persistent storage. Real hardware, UEFI input, and live debugging require
separate verification.

## Development Practice

Use small branches and pull requests, keep `main` bootable, and require CI before
releases. The starter already includes:

- `.github/workflows/ci.yml`
- `.github/pull_request_template.md`
- `CHANGELOG.md`
- `docs/release.md`
- `docs/roadmap.md`
- `docs/third_party.md`

The local project uses `https://github.com/Intality/ShawnOS.git` as its `origin`.
Follow `docs/release.md` to publish a versioned release after CI passes.

