# MyOS

MyOS is a small independent 64-bit x86-64 operating system starter written in
freestanding C++. It is not Debian, not Linux, and not a repackaged distro. The
v0.1.0 goal is intentionally modest: boot through Limine, enter a C++ kernel,
draw a welcome screen to the framebuffer, and prove first boot in QEMU.

Repository: [Intality/ShawnOS](https://github.com/Intality/ShawnOS). The kernel,
boot-screen label, and build artifacts currently use the temporary name MyOS.

## What Is Included

- Limine boot protocol setup.
- Higher-half x86-64 ELF kernel.
- Freestanding C++20 with no standard library, exceptions, or RTTI.
- A tiny assembly file for `hlt`, `inb`, and `outb`.
- Framebuffer welcome screen.
- Serial logging for test automation.
- ISO build, QEMU run, QEMU debug, and QEMU smoke-test targets.
- GitHub Actions CI that builds the ISO and runs the smoke test.
- Release notes and a small incremental roadmap.

## Prerequisites

Use Ubuntu in VirtualBox, WSL2, or a native Linux installation. Install:

```sh
sudo apt-get update
sudo apt-get install -y git build-essential clang lld curl xorriso qemu-system-x86
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

Run the automated first-boot smoke test:

```sh
make smoke TOOLCHAIN=llvm
```

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
- `kernel/src/framebuffer.cpp` draws the welcome screen using the framebuffer
  supplied by Limine.
- `kernel/src/serial.cpp` writes boot status messages to COM1 for QEMU tests.
- `kernel/src/arch/x86_64/cpu.S` contains the only assembly in v0.1.0.
- `kernel/src/lib/memory.cpp` supplies the memory routines compilers may emit.
- `kernel/src/lib/abi.cpp` supplies minimal C++ ABI hooks for freestanding code.
- `kernel/linker/x86_64.ld` places the kernel in the higher half and preserves
  the Limine request section.

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

