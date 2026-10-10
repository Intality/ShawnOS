# MyOS Roadmap

## v0.1.0 - First Boot

- Freestanding C++ kernel entry point.
- Limine boot protocol framebuffer request.
- Welcome screen and serial smoke-test marker.
- Bootable ISO build path.

## v0.2.0 - Early Kernel Groundwork

- Add a proper panic screen.
- Add physical memory map parsing.
- Add a frame allocator.
- Add a basic text console abstraction.

## v0.3.0 - Interrupts and Time

- Load a GDT and IDT.
- Handle CPU exceptions cleanly.
- Add PIT or HPET timer support.
- Start documenting architecture decisions in `docs/`.

## v0.4.0 - Keyboard and Shell Seed

- Add PS/2 keyboard input.
- Add a tiny command loop.
- Add more QEMU smoke tests.

