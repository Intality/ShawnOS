# Changelog

## v0.3.1 - VirtualBox Timer Compatibility (2026-10-11)

- Keep the local APIC enabled and configure LINT0 ExtINT virtual-wire delivery
  for the legacy PIC instead of relying on disabling IA32_APIC_BASE.
- Map the xAPIC register page explicitly as uncacheable device memory; preserve
  bootloader mappings and support an already-enabled x2APIC through its MSRs.
- Handle the APIC spurious vector without an EOI or an unexpected-interrupt panic.
- Replace unlimited tick-based clock calibration with independent, bounded PIT
  channel 2 polling. Verify sustained IRQ0 progress through the real desktop's
  idle/input loop in boot and interaction tests.
- Use PIT channel 0's mode 3 square wave for the approximately 100 Hz timer.
- Add regression tests for APIC and no-APIC boot, keyboard/timer/idle progress,
  repeated fresh boots, boot without a TSC, spurious interrupt return, and
  optional CPU interrupt tracing. Retain hardware diagnostics on desktop-test failures.
- Retain all v0.3.0 kernel groundwork and desktop functionality.

## v0.3.0 - Kernel Groundwork and Interrupts (Included in v0.3.1)

- Install a kernel-owned GDT, IDT, and TSS, preserving registers and stack alignment.
- Handle CPU exceptions with serial diagnostics and a framebuffer panic screen.
- Use dedicated stacks for double faults, NMIs, and machine checks.
- Validate Limine's memory map and allocate/release usable 4 KiB physical frames
  below 4 GiB, protecting reservations and rejecting invalid or duplicate frees.
- Verify allocation at boot by writing and reading two real physical pages.
- Run the PIT at approximately 100 Hz and sleep with `hlt` between input polls.
- Add live uptime, detected/free RAM with MiB decimals, CPU base or approximate
  TSC clock readings in About, and `mem`/`uptime`/`cpu` terminal commands.
- Expand unit tests, desktop tests, and CI to cover memory, timer progress, and
  four isolated real CPU fault cases, including the emergency double-fault stack.
- Document initialization, memory ownership, remaining limits, and release checks.
- Add an X11 SDL launch fallback and first-movement/click serial diagnostics
  for investigating physical mouse capture separately from QMP input tests.
- Refresh only About's visible uptime band instead of the full desktop every
  second, improving input responsiveness under software virtualization.
- Default interactive QEMU launches to GTK after verifying host mouse movement
  and clicks; SDL over X11 delivered clicks without motion on the tested host.
- Keep applications RAM-only and bundled into the kernel; no installed-program,
  disk filesystem, user-process isolation, or heap support is claimed.

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

