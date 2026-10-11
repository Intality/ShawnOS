# Changelog

## v0.2.0 - Kernel Desktop

- Boot directly into the ShawnOS desktop with launcher, taskbar, and wallpaper.
- Add PS/2 keyboard and mouse input with Shift, Caps Lock, and packet decoding.
- Add draggable, focusable windows with minimize, restore, and close controls.
- Add a built-in command terminal, session Notes, About, and Appearance apps.
- Clip rendering to validated framebuffer bounds and respect RGB masks and pitch.
- Add host unit tests and real QEMU desktop interaction tests at two resolutions.
- Extend CI to build and exercise the desktop and retain screenshot evidence.
- Retain MyOS artifact names and document the kernel-only, RAM-only limitations.

## v0.1.0 - First Boot

- Boot a freestanding 64-bit x86-64 C++ kernel with Limine.
- Request and draw to a 32-bit RGB framebuffer.
- Print a serial boot marker for automated QEMU smoke tests.
- Provide ISO build, run, debug, and CI targets.

