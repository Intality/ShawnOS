#include <stddef.h>
#include <myos/memory.hpp>
#include <myos/runtime.hpp>
#include <myos/serial.hpp>

extern "C" void myos_outb(uint16_t, uint8_t);
extern "C" uint8_t myos_inb(uint16_t);
extern "C" void myos_load_gdt(const void *);
extern "C" void (*myos_isr_table[256])();

namespace {
struct [[gnu::packed]] TablePointer { uint16_t limit; uint64_t address; };
struct [[gnu::packed]] Gate {
    uint16_t low, selector;
    uint8_t ist, attributes;
    uint16_t middle;
    uint32_t high, reserved;
};
struct [[gnu::packed]] Tss {
    uint32_t reserved0;
    uint64_t rsp[3], reserved1, ist[7], reserved2;
    uint16_t reserved3, iomap;
};
static_assert(sizeof(Gate) == 16 && sizeof(Tss) == 104 && sizeof(TablePointer) == 10);
static_assert(offsetof(myos::InterruptFrame, vector) == 120);
static_assert(offsetof(myos::InterruptFrame, rip) == 136);
alignas(16) uint64_t gdt[5] = {0, 0x00af9a000000ffff, 0x00cf92000000ffff, 0, 0};
alignas(16) Gate idt[256];
Tss tss = {};
alignas(16) uint8_t emergency_stacks[3][16384];
alignas(8) volatile uint64_t ticks = 0;
uint64_t wakes = 0;
uint64_t base_mhz = 0, counter_mhz = 0;

uint64_t read_msr(uint32_t reg) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(reg));
    return (static_cast<uint64_t>(high) << 32) | low;
}
void write_msr(uint32_t reg, uint64_t value) {
    asm volatile("wrmsr" :: "a"(static_cast<uint32_t>(value)),
        "d"(static_cast<uint32_t>(value >> 32)), "c"(reg) : "memory");
}

// Reserved device memory is not in Limine's direct map. Add one UC mapping,
// retaining the boot page tables and refusing to overwrite any existing leaf.
volatile uint32_t *map_apic(uint64_t physical, uint64_t offset, bool has_pat) {
    constexpr uint64_t address_mask = 0x000ffffffffff000ull;
    constexpr uint64_t window = 0xfffffe8000000000ull;
    uint64_t cr3, cr4;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    const auto table_at = [offset](uint64_t physical_address) -> uint64_t * {
        if (physical_address > UINT64_MAX - offset) { myos::panic("Page table address overflow"); }
        return reinterpret_cast<uint64_t *>(offset + physical_address);
    };
    uint64_t *table = table_at(cr3 & address_mask);
    for (int shift = (cr4 & (1u << 12)) ? 48 : 39; shift > 12; shift -= 9) {
        uint64_t &entry = table[(window >> shift) & 511];
        if (entry & 1) {
            if ((shift == 30 || shift == 21) && (entry & 0x80)) {
                myos::panic("APIC mapping window occupied");
            }
        } else {
            const uint64_t frame = myos::physical_frames().allocate();
            if (!frame) { myos::panic("No frame for APIC page table"); }
            uint64_t *next = table_at(frame);
            for (unsigned i = 0; i < 512; ++i) { next[i] = 0; }
            entry = frame | 3;
        }
        table = table_at(entry & address_mask);
    }
    uint64_t &leaf = table[(window >> 12) & 511];
    if (leaf & 1) { myos::panic("APIC mapping window occupied"); }
    unsigned cache_index = 3;
    if (has_pat) {
        const uint64_t pat = read_msr(0x277);
        for (cache_index = 0; cache_index < 8; ++cache_index) {
            if (((pat >> (cache_index * 8)) & 0xff) == 0) { break; }
        }
        if (cache_index == 8) { myos::panic("No uncacheable PAT entry"); }
    }
    const uint64_t cache_flags = ((cache_index & 1) ? 8 : 0) |
        ((cache_index & 2) ? 16 : 0) | ((cache_index & 4) ? 128 : 0);
    const uint64_t nx = (read_msr(0xc0000080) & (1u << 11)) ? (1ull << 63) : 0;
    leaf = physical | 3 | cache_flags | nx;
    asm volatile("invlpg (%0)" :: "r"(window) : "memory");
    return reinterpret_cast<volatile uint32_t *>(window);
}

void pic_delivery(uint64_t offset) {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    if (!(d & (1u << 9))) {
        myos::serial_write_line("MYOS: legacy PIC delivery ready");
        return;
    }
    const uint64_t base = read_msr(0x1b);
    if (!(base & (1u << 11))) {
        write_msr(0x1b, base | (1u << 11));
        if (!(read_msr(0x1b) & (1u << 11))) { myos::panic("Local APIC enable failed"); }
    }
    // Keep the enabled APIC and route the PIC through LINT0 ExtINT. Disabling
    // IA32_APIC_BASE is not a reliable legacy bypass on VirtualBox's NEM backend.
    if (base & (1u << 10)) {
        write_msr(0x808, 0);
        write_msr(0x80f, (read_msr(0x80f) & ~0xffull) | 0x1ff);
        write_msr(0x835, 0x700);
        if ((read_msr(0x835) & 0x10700) != 0x700) { myos::panic("APIC ExtINT setup failed"); }
        myos::serial_write_line("MYOS: x2APIC PIC virtual wire ready");
    } else {
        auto *apic = map_apic(base & 0x000ffffffffff000ull, offset, (d & (1u << 16)) != 0);
        apic[0x80 / 4] = 0;
        apic[0xf0 / 4] = (apic[0xf0 / 4] & ~0xffu) | 0x1ff;
        apic[0x350 / 4] = 0x700;
        if ((apic[0x350 / 4] & 0x10700) != 0x700) { myos::panic("APIC ExtINT setup failed"); }
        myos::serial_write_line("MYOS: xAPIC PIC virtual wire ready");
    }
}

uint64_t read_counter() {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0) : "memory");
    asm volatile("rdtsc" : "=a"(a), "=d"(d) :: "memory");
    return (static_cast<uint64_t>(d) << 32) | a;
}

// Measure independently of IRQ0, retaining a finite poll cap and restoring
// the caller's interrupt state before returning to the desktop.
bool pit_interval(bool has_tsc, uint64_t &first, uint64_t &last) {
    uint64_t flags;
    asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    const uint8_t mask = myos_inb(0x21);
    myos_outb(0x21, mask | 1);
    const uint8_t saved = myos_inb(0x61);
    myos_outb(0x61, saved & ~3u);
    myos_outb(0x43, 0xb0);
    myos_outb(0x42, 0xff);
    myos_outb(0x42, 0xff);
    first = has_tsc ? read_counter() : 0;
    myos_outb(0x61, (saved & ~3u) | 1);
    bool finished = false;
    for (unsigned i = 0; i < 1000000; ++i) {
        if (myos_inb(0x61) & 0x20) { finished = true; break; }
        // Pace device-port reads during the measurement.
        for (unsigned delay = 0; delay < 256; ++delay) { asm volatile("pause"); }
    }
    last = has_tsc ? read_counter() : 0;
    myos_outb(0x61, saved);
    myos_outb(0x21, mask);
    if (flags & (1u << 9)) { asm volatile("sti; nop" ::: "memory"); }
    return finished;
}

void pic_write(uint16_t port, uint8_t value) {
    myos_outb(port, value);
    myos_outb(0x80, 0);
}
bool equal(const char *a, const char *b) {
    if (!a) { return false; }
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
const char *exception_name(uint64_t vector) {
    const char *names[32] = {
        "Divide error", "Debug exception", "Non-maskable interrupt", "Breakpoint",
        "Overflow", "Bound range exceeded", "Invalid opcode", "Device unavailable",
        "Double fault", "Reserved exception", "Invalid TSS", "Segment not present",
        "Stack fault", "General protection fault", "Page fault", "Reserved exception",
        "Floating point exception", "Alignment check", "Machine check", "SIMD exception",
        "Virtualization exception", "Control protection", "Reserved exception", "Reserved exception",
        "Reserved exception", "Reserved exception", "Reserved exception", "Reserved exception",
        "Hypervisor injection", "VMM communication", "Security exception", "Reserved exception"
    };
    return names[vector];
}
}

extern "C" void myos_interrupt_dispatch(const myos::InterruptFrame *frame) {
    if (frame->vector < 32) {
        if (frame->vector == 8) {
            const uint64_t pointer = reinterpret_cast<uint64_t>(frame);
            const uint64_t start = reinterpret_cast<uint64_t>(emergency_stacks[0]);
            if (pointer < start || pointer > start + sizeof(emergency_stacks[0]) - sizeof(*frame)) {
                myos::panic("Invalid double fault stack", frame);
            }
            myos::serial_write_line("MYOS: double fault emergency stack");
        }
        uint64_t address = 0;
        if (frame->vector == 14) { asm volatile("mov %%cr2, %0" : "=r"(address)); }
        myos::panic(exception_name(frame->vector), frame, address);
    }
    if (frame->vector == 32) {
        ticks = ticks + 1;
        myos_outb(0x20, 0x20);
        return;
    }
    // APIC spurious interrupts and ExtINT do not receive a local APIC EOI.
    if (frame->vector == 255) { return; }
    // A spurious IRQ7 gets no EOI; a spurious IRQ15 acknowledges only the cascade.
    if (frame->vector == 39 || frame->vector == 47) {
        const uint16_t port = frame->vector == 39 ? 0x20 : 0xa0;
        myos_outb(port, 0x0b);
        if (!(myos_inb(port) & 0x80)) {
            if (frame->vector == 47) { myos_outb(0x20, 0x20); }
            return;
        }
    }
    myos::panic("Unexpected interrupt", frame);
}

namespace myos {
void interrupts_init() {
    asm volatile("cli" ::: "memory");
    for (int i = 0; i < 3; ++i) {
        tss.ist[i] = reinterpret_cast<uint64_t>(&emergency_stacks[i][sizeof(emergency_stacks[i])]);
    }
    tss.iomap = sizeof(Tss);
    const uint64_t base = reinterpret_cast<uint64_t>(&tss);
    gdt[3] = (sizeof(Tss) - 1) | ((base & 0xffffff) << 16) |
        (0x89ull << 40) | (((base >> 24) & 0xff) << 56);
    gdt[4] = base >> 32;
    const TablePointer gdtr = {sizeof(gdt) - 1, reinterpret_cast<uint64_t>(gdt)};
    myos_load_gdt(&gdtr);
    for (int i = 0; i < 256; ++i) {
        const uint64_t handler = reinterpret_cast<uint64_t>(myos_isr_table[i]);
        idt[i] = {static_cast<uint16_t>(handler), 8,
            static_cast<uint8_t>(i == 8 ? 1 : i == 2 ? 2 : i == 18 ? 3 : 0), 0x8e,
            static_cast<uint16_t>(handler >> 16), static_cast<uint32_t>(handler >> 32), 0};
    }
    const TablePointer idtr = {sizeof(idt) - 1, reinterpret_cast<uint64_t>(idt)};
    asm volatile("lidt %0" :: "m"(idtr) : "memory");
    serial_write_line("MYOS: GDT IDT TSS ready");
}
void timer_start(uint64_t direct_map_offset) {
    asm volatile("cli" ::: "memory");
    pic_delivery(direct_map_offset);
    pic_write(0x21, 0xff); pic_write(0xa1, 0xff);
    pic_write(0x20, 0x11); pic_write(0xa0, 0x11);
    pic_write(0x21, 0x20); pic_write(0xa1, 0x28);
    pic_write(0x21, 4); pic_write(0xa1, 2);
    pic_write(0x21, 1); pic_write(0xa1, 1);
    constexpr uint16_t divisor = 1193182 / TIMER_HZ;
    // Use a square wave for the approximately 100 Hz IRQ0 source.
    myos_outb(0x43, 0x36);
    myos_outb(0x40, static_cast<uint8_t>(divisor));
    myos_outb(0x40, static_cast<uint8_t>(divisor >> 8));
    pic_write(0xa1, 0xff);
    pic_write(0x21, 0xfe);
    serial_write_line("MYOS: PIT ready 100 Hz");
    asm volatile("sti" ::: "memory");
}
uint64_t cpu_base_mhz() { return base_mhz; }
uint64_t tsc_mhz() { return counter_mhz; }
void clock_init() {
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    const uint32_t maximum = a;
    if (maximum >= 0x16) {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x16), "c"(0));
        base_mhz = a & 0xffff;
    }
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    const bool has_tsc = (d & (1u << 4)) != 0;
    uint64_t first, last;
    if (!pit_interval(has_tsc, first, last)) { panic("PIT calibration did not complete"); }
    if (has_tsc && last > first && last - first <= UINT64_MAX / 1193182) {
        counter_mhz = (last - first) * 1193182 / (65535ull * 1000000);
    }
    serial_write_line(base_mhz ? "MYOS: CPU base frequency reported" : "MYOS: CPU base frequency unavailable");
    serial_write_line(counter_mhz ? "MYOS: clock counter calibrated" : "MYOS: clock counter unavailable");
}
uint64_t timer_ticks() { return ticks; }
uint64_t idle_wakes() { return wakes; }
void idle_wait() {
    asm volatile("sti; hlt" ::: "memory");
    ++wakes;
}
void exception_selftest(const char *command_line) {
    if (equal(command_line, "selftest=apic-spurious")) {
        asm volatile("int $255" ::: "memory");
        serial_write_line("MYOS: APIC spurious self-test passed");
    } else if (equal(command_line, "selftest=ud2")) {
        asm volatile("ud2");
    } else if (equal(command_line, "selftest=divide")) {
        asm volatile("xor %%edx, %%edx; xor %%ecx, %%ecx; mov $1, %%eax; div %%ecx" ::: "rax", "rcx", "rdx");
    } else if (equal(command_line, "selftest=pagefault") || equal(command_line, "selftest=doublefault")) {
        if (equal(command_line, "selftest=doublefault")) { idt[14].attributes = 0; }
        asm volatile("xor %%rax, %%rax; movq $1, (%%rax)" ::: "rax", "memory");
    }
}
}
