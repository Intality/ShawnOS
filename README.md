# ShawnOS

ShawnOS is a small independent 64-bit x86-64 operating system written in
freestanding C++. It is not Debian, not Linux, and not a repackaged distro. The
v0.3.1 kernel boots through Limine into an interactive framebuffer desktop.

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
- Terminal with `help`, `clear`, `version`, `echo`, `about`, `notes`, `wallpaper`, `mem`, `uptime`, and `cpu`.
- Notes editor with typing, newline, backspace, wrapping, and session retention.
- About and Appearance apps; Alpine and Dusk wallpaper choices.
- Live uptime and free-memory readings in About; `mem` and `uptime` commands.
- Kernel-owned GDT, IDT, and TSS with emergency exception stacks.
- CPU exception diagnostics and a framebuffer panic screen.
- Validated physical memory map and a tested 4 KiB frame allocator.
- Approximately 100 Hz PIT timer and `hlt` idle between input checks.
- APIC/PIC virtual-wire routing and independent, bounded clock calibration.
- Serial logging for test automation.
- ISO build, QEMU run, QEMU debug, and QEMU smoke-test targets.
- Unit tests and QMP-driven desktop interaction tests at 1024x768 and 640x480.
- QEMU tests for invalid opcode, divide error, page fault, and double fault.
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

Run/debug use QEMU's GTK display by default. Use `QEMU_DISPLAY=sdl` to choose
SDL, or `make run-mouse TOOLCHAIN=llvm` for the X11 SDL fallback.

Click inside the QEMU desktop before moving the mouse: the PS/2 mouse needs
QEMU to capture host input. Ctrl+Alt+G toggles
mouse and keyboard capture. Press F1 to check keyboard input by opening Terminal.
If uptime advances but the pointer stays still, check capture first.

If both uptime and input stop, that is not just mouse capture. v0.3.1 configures
the APIC/PIC delivery path needed to wake the desktop. For VirtualBox, mount the newly built ISO and start
from Powered Off; resuming a saved state continues the old kernel in memory.
Use a PS/2 pointing device and keyboard. Guest Additions are not supported.

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

This includes native AddressSanitizer and UndefinedBehaviorSanitizer checks. Run
only those checks with `make test-host-sanitized`.

Test the smaller display mode:

```sh
make iso TOOLCHAIN=llvm ISO=build/myos-640.iso LIMINE_CONFIG=tests/limine-640.conf
python3 scripts/desktop-test.py build/myos-640.iso --expect-size 640x480 --memory 128M --output-dir build/desktop-test-640
```

Run isolated CPU exception tests:

```sh
make panic-test TOOLCHAIN=llvm
```

Run interrupt-routing and idle regression tests:

```sh
make timer-test TOOLCHAIN=llvm
```

This checks APIC and no-APIC boot, advancing uptime/idle counters, keyboard input,
repeated fresh boots, boot without a TSC, and spurious interrupt return.
It is included in `make test`. Its isolated ISOs are not release assets.

An extended boot check is available with `python3 scripts/timer-test.py --boot-repeats 15`.
Use `--trace-interrupts` to retain CPU interrupt traces when diagnosing a failure.

This creates separate test ISOs under `build/panic-tests/`; the normal ISO has no
crash-test menu entries. Tests verify the actual CPU exception vector and error
code, page-fault address, emergency double-fault stack, panic pixels, and halted
CPU state. Expected panic screens are test successes, not failures.

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

- `kernel/src/boot.cpp` defines the Limine requests and the `kmain` kernel entry,
  validates boot memory, and checks allocation with real physical page writes.
- `kernel/src/graphics.cpp` clips drawing, renders the built-in bitmap font,
  handles RGB masks and pitch, and copies pixels to Limine's framebuffer.
- `kernel/src/desktop.cpp` owns the desktop, window stacking, apps, input loop,
  wallpaper, and cursor. Its contiguous backing buffers remain statically sized.
- `kernel/src/memory.cpp` manages usable 4 KiB physical frames; the same allocator
  is tested natively for exhaustion, alignment, reservations, and invalid frees.
- `kernel/src/arch/x86_64/interrupts.cpp` owns descriptor tables, exception stacks,
  PIC setup, and PIT ticks. `interrupts.S` preserves registers across interrupts.
- `kernel/src/panic.cpp` reports fatal exceptions to COM1 and the framebuffer.
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
but not rebooting. QEMU's default GTK window offers View > Grab Input;
Ctrl+Alt+G toggles capture.

If the physical mouse does not move the guest cursor on Ubuntu, click inside
the QEMU window and press Ctrl+Alt+G to toggle capture. On a Wayland session,
the default GTK backend delivers physical mouse movement on the tested host,
where SDL over X11 delivered clicks but no motion. As an alternate backend,
try `make run-mouse TOOLCHAIN=llvm` to use SDL through X11/XWayland.
This requires an available X11 display and QEMU's SDL backend. Normal `make run`
and `make run-mouse` save boot and input diagnostics to `build/run-serial.log`.
`mouse ready` confirms device initialization; `mouse movement received` and
`mouse click received` confirm that actual input reached the desktop. Automated
QMP tests bypass host capture, so passing those tests cannot verify a physical
mouse. ShawnOS currently supports PS/2 input; do not add a USB tablet device.

## Current Limits

This is a real early kernel desktop, not a complete general-purpose OS. Apps all
run inside one kernel loop, not as isolated processes. There is no disk-backed
filesystem, USB input driver, networking, sound, or installed-program support.
Terminal commands are built-ins, not Linux programs. Notes has a 2047-character
RAM limit. Desktop resolutions supported are 640x480 through 1920x1200, RGB32;
the normal ISO requests 1024x768.

Input still uses bounded PS/2 polling; only the timer IRQ is enabled. The CPU
sleeps with `hlt` when input is exhausted and wakes approximately every 10 ms.
The timer is not a wall clock and does not schedule processes. The frame
allocator manages all fully covered usable RAM pages in the firmware map,
including RAM above 4 GB. Bootloader, kernel, ACPI, framebuffer, and device
reservations are not available for allocation. Its own tracking storage is
reserved too. `mem` distinguishes detected, usable, managed, and free RAM.
Physical frame allocation is not a C++ heap or a user-process memory system.
Exceptions stop the session, so unsaved Notes text is lost on restart. Very early
boot failures before framebuffer setup report only to serial. See
`docs/architecture.md` for initialization order and the remaining constraints.
Real hardware, UEFI input, SMP, and live debugging require separate verification.

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


## Hardware Readings

About and `mem` use the familiar 1024-based RAM display, with MB/GB labels and
two decimal places rounded half up. In this display convention, MB represents
1,048,576 bytes and GB represents 1,073,741,824 bytes: a 4096 RAM setting in
VirtualBox corresponds to 4.00 GB, not 4.29 GB. These are deliberately not decimal
SI conversions. Values below 1,073,741,824 bytes use MB; values at or above it
use GB. A value just below the boundary can round to 1024.00 MB. This only
changes presentation. Detected RAM
is the union of RAM regions identified in Limine's map: usable RAM, kernel/modules,
bootloader memory, and ACPI memory. Ambiguous reserved ranges and MMIO are excluded,
so it can be slightly below configured or installed capacity. `mem` keeps detected,
usable, managed, and free RAM distinct. The allocator no longer has a 4 GB
address ceiling: it tracks usable regions above device-address gaps as well as
low RAM. Free RAM excludes allocator bookkeeping and outstanding allocations.
Raw byte counts remain in serial test diagnostics.

Desktop tests check the 1024-based conversion and the exact memory text drawn in About.
The low-memory check uses one CPU and QEMU's `--memory 64M` setting (67,108,864
assigned bytes; detected/free RAM is lower). Larger checks use `1536M` and `4096M`:

```sh
python3 scripts/desktop-test.py build/myos-640.iso --expect-size 640x480 --memory 64M --output-dir build/memory-test-64
python3 scripts/desktop-test.py build/myos-x86_64.iso --memory 1536M --output-dir build/memory-test-gb
python3 scripts/desktop-test.py build/myos-x86_64.iso --memory 4096M --output-dir build/memory-test-4096
```

Run the physical-memory capacity matrix in Ubuntu:

```sh
make memory-test TOOLCHAIN=llvm
python3 scripts/memory-test.py --memory 64 1024 4096 8192 16384
python3 scripts/memory-test.py --memory 64 1023 1024 1025 16384 --resolution 640x480 --output-dir build/memory-tests-640
```

Each run builds a separate test-only ISO with `selftest=memory`. It allocates
every available frame until exhaustion, then checks that every non-metadata
usable page can be released and that the original free count is restored.
Normal boots also write/read physical pages above 4 GB and at the top of usable
RAM when high memory exists. The matrix then checks desktop input and visible
memory readings. It uses QEMU's on-demand host memory backing and a 1.5 GB
resident-memory safety limit, so testing a 16 GB guest does not require filling
16 GB of host RAM. This is not an exhaustive read/write test of every RAM byte.
Native tests also exhaust simulated 32 GB RAM and exercise sparse high addresses;
those do not certify a 32 GB VM or physical hardware. Results and failures are
saved in fresh folders under `build/memory-tests/`.

About and `cpu` label the hardware-reported frequency as **CPU base speed**.
Intel CPUs that expose CPUID leaf 0x16 provide this value in MHz. If it is missing
or zero, an explicit Intel brand-string suffix such as `@ 2.40GHz` or `@ 800MHz`
can supply the nominal base specification. The brand is queried from the running
CPU, not a model-name lookup table or a fixed build-time value. Unsupported
vendors and brands without a valid suffix use a vendor-independent SMBIOS
startup-speed fallback, labelled **CPU boot speed** rather than base speed.
Missing or invalid firmware data still reports **Unavailable** without preventing boot.
Values below 1,000 MHz are displayed in MHz; higher values use GHz, rounded to two decimal
places. Missing, zero, or unsupported frequency data is shown as **Unavailable**.
This is a static base specification, not live turbo or overclocked speed, and a
virtual machine may not expose it. About never substitutes a timer-counter rate.

Intel describes the brand-string frequency as the manufacturing-time base
specification in its [frequency display advisory](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/processor-frequency-display-mismatch-windows-advisory.pdf).
This fallback does not measure current execution speed or a configured cTDP point.

The `cpu` command retains a separate TSC clock diagnostic, measured against a PIT
channel 2 interval at boot and shown with `~`. The TSC reading is a clock-counter
rate, not CPU execution speed or host speed.
The separately labeled PIT / 100 Hz reading is the system timer frequency.

The [VirtualBox CPU reporting lab](docs/virtualbox-cpu-tests.md) runs the same ISO
against different simulated CPU frequency reports, including the MHz/GHz boundary
and missing-data cases, without changing existing VMs or the physical CPU clock.

`make cpu-test` boots the same ISO with AMD, Intel, and other vendor-ID simulations
using both SMBIOS entry formats. See [CPU compatibility](docs/cpu-support.md) for
source labels, coverage, platform requirements, and remaining limits. A missing
speed reading is not an unsupported-CPU boot error; universal hardware compatibility
has not been established.
