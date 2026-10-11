# MyOS Roadmap

## v0.1.0 - First Boot

- Freestanding C++ kernel entry point.
- Limine boot protocol framebuffer request.
- Welcome screen and serial smoke-test marker.
- Bootable ISO build path.

## v0.2.0 - Interactive Kernel Desktop

- Framebuffer desktop, launcher, taskbar, and original wallpaper.
- Polling PS/2 keyboard and mouse input.
- Movable windows, focus, minimize, restore, and close.
- Built-in Terminal, session Notes, About, and Appearance apps.
- Host tests and QEMU input/screenshot integration tests.

## v0.3.0 - Kernel Groundwork and Interrupts

- Add a proper panic screen.
- Add physical memory map parsing.
- Add a frame allocator.
- Load a GDT and IDT.
- Handle CPU exceptions cleanly.
- Add PIT or HPET timer support.
- Start documenting architecture decisions in `docs/`.

## v0.4.0 - Persistent Apps

- Add a block driver and a small filesystem.
- Save and load Notes documents.
- Add memory allocation and a foundation for isolated user processes.
- Move input and idle handling from polling to interrupts.
- Add more QEMU storage and exception tests.

