# x86-64 CPU Compatibility and Speed Reporting

ShawnOS targets baseline `x86-64`, not a host-specific instruction set. CPU vendor
does not determine whether the kernel is allowed to boot. This is a development
target, not certification that every x86-64 processor or motherboard has been
tested. Boot firmware, framebuffer, PS/2 input, legacy PIT/PIC delivery, and the
other current platform requirements still matter. Only the bootstrap CPU runs.

## Speed Sources

The display selects the first available trustworthy report:

1. Intel's documented CPUID 0x16 base-frequency field, when supported and nonzero.
2. An explicit Intel CPUID brand-string frequency suffix, as a nominal base report.
3. SMBIOS Type 4 Current Speed, from an enabled, populated processor socket.
   This is vendor-independent: AMD, Intel, Hygon, VIA/Centaur, Zhaoxin, unknown
   vendors, and virtual CPUs use the same firmware parser.
4. `Unavailable` if no valid report exists. Boot continues normally.

The first two paths are labelled **CPU base speed**. The firmware fallback is
labelled **CPU boot speed** because the standard defines it as startup speed,
not necessarily rated base frequency or current turbo speed. The SMBIOS Max Speed
field is a system capability, so it is never substituted for processor base speed.
No CPU-model lookup table, build-time speed, TSC estimate, benchmark estimate, or
host-specific constant is used. VM-provided data describes the virtual machine;
it is not evidence of physical-host speed or a performance limit.

There is no mandatory universal x86-64 base-frequency report. Complete numerical
coverage cannot be guaranteed for hardware or VMs that withhold that information.
Vendor-specific P-state/MSR backends, live effective speed, ACPI CPPC, per-core
hybrid-CPU readings, and real-hardware certification remain separate work.

## Firmware Safety

The kernel requests SMBIOS through Limine base revision 6. Both SMBIOS 2.x
32-bit and 3.x 64-bit entry points are supported. Entry checksums, signatures,
lengths, physical-address arithmetic, and mapped-memory coverage are validated
before following table pointers. Only reserved-mapped memory is read. SMBIOS 3.x
uses a maximum table size; scanning stops at its end marker inside mapped memory.
SMBIOS 2.x additionally validates its structure count and exact table length.

Scanning is bounded to 4 MB, never changes firmware or CPU registers, and checks
structure lengths and double-NUL string terminators. Inactive structures,
unpopulated sockets, and disabled CPUs are ignored. Unknown or conflicting
enabled-socket speeds are rejected instead of selecting an arbitrary socket.
Malformed or missing SMBIOS data leaves the other sources and boot path intact.

## Tests

```sh
make test-host
make test-host-sanitized
make cpu-test
```

The CPU matrix boots the same ISO with nine CPU/report combinations at 64 MB RAM,
including Opteron G1, EPYC, Nehalem, vendor-ID simulations, both SMBIOS entry-point
formats, the 999 MHz / 1.00 GHz boundary, missing data, and Intel base-report
priority over conflicting firmware data. Each case also runs the full desktop
interaction test, including timer progress and keyboard/mouse input. Results,
serial logs, and screenshots are saved under `build/cpu-tests/`.

Host tests exercise malformed/truncated tables, checksums, address overflow,
memory-map gaps, multiple sockets, source priority, and 10,000 generated malformed
buffers under address/undefined-behavior sanitizers. CI runs the same matrix.
Passing these tests does not establish universal physical-hardware compatibility.

## References

- [DMTF SMBIOS specification](https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.9.0.pdf), entry points and Type 4 Current Speed.
- [Limine protocol](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md), base revisions 5/6 and SMBIOS mappings.
- [Intel architecture manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html), CPUID processor-frequency information.
- [AMD architecture manuals](https://docs.amd.com/api/khub/documents/sD1_QL~h4Afq2_tvzxqqSQ/content), vendor-specific performance interfaces, not a universal base-MHz field.
