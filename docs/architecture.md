# v0.3 Kernel Foundations

## Boot Order

1. Disable interrupts and initialize COM1 serial output.
2. Check Limine base revision 6 and run freestanding global constructors.
3. Install kernel-owned GDT, TSS, and all 256 IDT gates, leaving IRQs masked.
4. Validate the Limine physical map and initialize physical frame bookkeeping.
5. Allocate two frames, access them through Limine's higher-half direct map,
   write/read distinct patterns, clear them, and return them to the allocator.
6. Initialize the validated framebuffer desktop and polled PS/2 devices.
7. Configure APIC LINT0 ExtINT virtual-wire delivery (or direct PIC delivery on
   a CPU without APIC), remap the PIC, program PIT channel 0, enable only IRQ0,
   then enable IF.
8. Poll PIT channel 2 to measure the TSC independently with a finite wait cap.
   A stuck calibration channel stops with a diagnostic.
9. Run the desktop loop; periodic timer interrupts wake its `hlt` idle state.

The main boot stack, page tables, and Limine response data remain owned by the
bootloader's reserved/reclaimable regions. They are not freed or reused.

## Physical Memory

`FrameAllocator` stores two static bitmaps: one for eligible frames and one for
outstanding allocations. Only `LIMINE_MEMMAP_USABLE` entries are eligible.
Physical address zero is deliberately excluded as the allocation-failure value.
Only completely covered, 4 KiB aligned pages are eligible. Allocation returns a
physical address, not a pointer; the boot self-test adds the reported HHDM offset
before touching it. Returned frames are not automatically zeroed.

The map must be sorted by base, have at most 4096 entries, and have no null entry
or overflowing range. Usable entries cannot overlap other entries; overlaps
between reserved entries are allowed by the protocol and are handled correctly.
Kernel/modules, framebuffer, bad RAM, ACPI, and bootloader regions are excluded.
Misaligned, reserved, out-of-range, unallocated, and double releases fail without
changing the free count. Exhaustion returns zero. There is no heap or ownership
tracking beyond outstanding frame allocation, and no contiguous allocation API.

The managed address range is [4 KiB, 4 GiB). Reported usable bytes can include
RAM above that ceiling, so `mem` reports usable, managed, and free RAM separately.
This implementation is single-CPU and called only from the main kernel context;
neither the timer handler nor another CPU allocates frames.

## Exceptions and Timer

The GDT has null, ring-0 code/data, and 64-bit TSS descriptors. The TSS supplies
separate 16 KiB IST stacks for double faults, NMIs, and machine checks. There are
no ring-3 segments or privilege transitions. The assembly stubs normalize CPU
error codes, save all 15 general registers, clear DF, align the stack for the
System V C++ call, and restore the exact interrupt frame through `iretq`.
SIMD/FPU state is not supported by this integer-only kernel.

Limine enables the local APIC but masks its LINT entries, so enabling the PIC
alone does not deliver interrupts. v0.3.1 keeps the APIC enabled and configures
LINT0 to receive the PIC's ExtINT signal (virtual-wire mode). It sets TPR to zero,
enables the spurious vector 255, and checks LINT0's delivery/mask bits. A CPU
without APIC retains the direct PIC path. An already-enabled x2APIC uses MSRs;
the kernel does not switch APIC modes. Other LVT sources remain masked.

The xAPIC register page is reserved device memory, not guaranteed to appear in
Limine's HHDM. A dedicated virtual page at `0xfffffe8000000000` is mapped with
an uncacheable PAT entry (or PCD/PWT on a CPU without PAT), supervisor writable,
and NX when enabled. The mapper walks the active four- or five-level tables,
zeroes newly allocated table frames, invalidates the translation, and refuses
to replace any existing leaf/large-page mapping. These few table frames remain
allocated for the boot's lifetime. No general virtual-memory allocator is added.

ExtINT and APIC spurious delivery do not require a local APIC EOI. IRQ0 still
acknowledges the master PIC only. The virtual-wire path is single-CPU legacy
compatibility, not I/O APIC routing, an APIC timer, or SMP support.

PIT channel 0 uses mode 3 and divisor 11931, approximately 100 Hz. Its square-wave
output avoids the single-clock high pulse of mode 2 on emulated paths. The PIC maps
IRQs to vectors 32-47; only IRQ0 is unmasked. The IRQ0 handler increments a
64-bit tick count and acknowledges the master PIC. It does not draw or log.
Spurious IRQ7/IRQ15 use the controller's in-service bits and the required EOI
behavior. Unhandled real interrupts stop the kernel with diagnostics.

PS/2 IRQs remain disabled. The main loop drains bounded input batches, updates
About's uptime band once per second when it is the top window, and executes `sti; hlt` if no complete event
is ready. Input may wait until the next timer tick (roughly 10 ms); the persistent
timer wakeup prevents a missed input event from sleeping forever. This is idle
management, not a scheduler, and uptime is nominal timer time, not real time.

Panic disables maskable interrupts, logs the reason, vector, error, saved RIP/RSP,
and CR2 for page faults, paints a diagnostic screen if a valid canvas is attached,
and halts. Recursive panic logs a fallback message and halts. Early failures have
serial diagnostics only. Recovery and preservation of RAM-only documents are not
implemented; reset starts a new session.

## Verification

- Native unit tests exercise the actual allocator, including exhaustion, invalid
  maps, reservations, reuse, and the 4 GiB boundary.
- Boot checks read/write actual allocated pages through HHDM.
- Desktop tests verify live uptime pixels, advancing tick/idle counters, memory
  commands, and all v0.2 interactions at 1024x768/256 MiB and 640x480/128 MiB.
- `panic-test.py` generates isolated ISOs with exact `selftest=ud2`,
  `selftest=divide`, `selftest=pagefault`, or `selftest=doublefault` boot arguments.
  Inline Assembly triggers real faults, not C++ undefined behavior. The double
  fault case makes the page-fault gate non-present before an unmapped write;
  exception delivery escalates to vector 8 and verifies the handler's IST stack.
- Fault tests check vector/error/RIP, page-fault CR2, diagnostic framebuffer
  pixels, no reboot, and QEMU's halted CPU state. They are not part of normal boot.
- `timer-test.py` repeats APIC and direct-PIC boots three times each, checks
  four advertised CPUs (only the BSP runs the kernel), boot without a TSC,
  keyboard/uptime/idle progress, and
  `selftest=apic-spurious` returning through vector 255. This argument is test-only.
  Tests require sustained delivered ticks and actual idle wakes, not just the
  boot-success marker. `--boot-repeats` controls the stress count, and
  `--trace-interrupts` retains QEMU CPU interrupt traces.

UEFI boot, physical hardware, NMI/machine-check injection, broken-stack recovery,
five-level MMIO mapping, x2APIC virtual-wire delivery, SMP, I/O APIC/HPET,
process scheduling, and user-mode execution require separate verification.

## References

- [Limine protocol specification](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md): memory ownership, HHDM, map guarantees, and machine state.
- [Intel architecture manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html): Volume 3A, descriptor tables, exception frames, IST, and interrupt return.

## RAM and Clock Readings

The detected-RAM count unions sorted firmware regions for usable, executable,
bootloader-reclaimable, ACPI-reclaimable, and ACPI-NVS memory, without double
counting overlaps. Reserved/MMIO, framebuffer, and bad-memory regions are excluded.
It describes identified RAM, not an installed-DIMM capacity measurement. The count
includes RAM above 4 GiB even though allocation remains limited below that address.

At boot, CPU identity and the maximum basic CPUID leaf are checked before reading
Intel leaf 0x16's reported CPU base MHz. Only the defined low 16 bits of EAX are
used. When that value is absent or zero, Intel's extended CPUID brand leaves
0x80000002-0x80000004 are read only after checking the maximum extended leaf.
A bounded parser accepts an explicit `@ numberGHz` or `@ numberMHz` suffix as a
nominal base specification. The 48-byte input is bounded even without a NUL;
ambiguous, malformed, overflowing, and fractional-MHz values are rejected.
There are no CPU model matches or model-to-frequency tables. A vendor-independent
SMBIOS Type 4 startup-speed fallback handles other vendors or missing base reports.
Both SMBIOS entry formats are requested through Limine; entry checksums, bounded
structure parsing, and reserved-mapped physical-range checks protect all reads.
The fallback is labelled CPU boot speed, never CPU base speed. Missing, malformed,
or conflicting firmware reports remain unavailable without stopping boot.
Serialized RDTSC samples across PIT channel 2's 65535-count one-shot interval
(about 55 ms) estimate TSC MHz independently of timer interrupt delivery. The
speaker and IRQ0 delivery are disabled during the measurement; the prior PIC
mask, gate bits, and CPU interrupt-enable state are restored afterward.
Polling has a finite iteration cap and computation between port reads. A
stuck channel 2 produces a panic. Sustained IRQ0 delivery is verified by the
automated desktop and repeated-boot tests, after entering the real idle loop.
The kernel does not yet have an independent runtime interrupt watchdog; loss
of IRQ0 delivery can still stall an idle CPU. This boot estimate is labeled TSC
clock in the diagnostic `cpu` command and is never used as CPU base speed.

About and `cpu` share a bounded integer-only formatter for CPU speed. Values
below 1,000 MHz use MHz; values at or above that threshold use GHz rounded to two
decimal places. Unavailable base frequency displays `Unavailable`, with no TSC
fallback. Base and boot sources remain distinct; neither is a measurement of
current turbo speed or overclocking. The CPUID guards and formatter boundaries
and brand parsing are covered by the normal and sanitizer-enabled host tests,
including different reported frequencies, unsupported vendors, truncation,
embedded NULs, and integer-overflow boundaries.

See [CPU compatibility](cpu-support.md) for the firmware parser's limits and the
cross-vendor QEMU boot matrix. Vendor-ID simulations do not certify real chips.

About refreshes only its uptime band while unobscured, so periodic updates do not monopolize the input loop in software virtualization. Bringing it to the front redraws current readings.
