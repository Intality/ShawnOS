#include <stddef.h>
#include <stdint.h>

#include <limine.h>
#include <myos/desktop.hpp>
#include <myos/serial.hpp>
#include <myos/memory.hpp>
#include <myos/runtime.hpp>
#include <myos/smbios.hpp>

extern "C" void myos_halt_forever();

extern "C" void (*__init_array[])();
extern "C" void (*__init_array_end[])();

namespace {

__attribute__((used, section(".limine_requests_start")))
volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

__attribute__((used, section(".limine_requests")))
volatile limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0, .response = nullptr,
};
__attribute__((used, section(".limine_requests")))
volatile limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0, .response = nullptr,
};
__attribute__((used, section(".limine_requests")))
volatile limine_executable_cmdline_request cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID, .revision = 0, .response = nullptr,
};

__attribute__((used, section(".limine_requests")))
volatile limine_smbios_request smbios_request = {
    .id = LIMINE_SMBIOS_REQUEST_ID, .revision = 0, .response = nullptr,
};

__attribute__((used, section(".limine_requests_end")))
volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

void call_global_constructors() {
    for (size_t i = 0; &__init_array[i] != __init_array_end; ++i) {
        __init_array[i]();
    }
}

void memory_init() {
    auto *map = memmap_request.response;
    auto *direct_map = hhdm_request.response;
    auto &frames = myos::physical_frames();
    if (!map || !direct_map || !frames.init(map->entries, map->entry_count)) {
        myos::panic("Invalid or unsupported memory map");
    }
    myos::serial_write_line("MYOS: memory map ready");
    const uint64_t before = frames.free_frames();
    const uint64_t first = frames.allocate(), second = frames.allocate();
    if (!first || !second || first == second || direct_map->offset > UINT64_MAX - second ||
        direct_map->offset > UINT64_MAX - first) { myos::panic("Frame allocation failed"); }
    auto *a = reinterpret_cast<volatile uint64_t *>(first + direct_map->offset);
    auto *b = reinterpret_cast<volatile uint64_t *>(second + direct_map->offset);
    for (unsigned i = 0; i < 512; ++i) { a[i] = 0x51a7c0de00000000ull + i; b[i] = ~a[i]; }
    for (unsigned i = 0; i < 512; ++i) {
        if (a[i] != 0x51a7c0de00000000ull + i || b[i] != ~a[i]) { myos::panic("Frame write/read failed"); }
    }
    for (unsigned i = 0; i < 512; ++i) { a[i] = 0; b[i] = 0; }
    if (!frames.release(first) || !frames.release(second) || frames.free_frames() != before) {
        myos::panic("Frame release failed");
    }
    myos::serial_write_line("MYOS: frame allocator self-test passed");
}

} // namespace

extern "C" void kmain() {
    asm volatile("cli" ::: "memory");
    myos::serial_init();
    myos::serial_write_line("MYOS: kernel entry");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        myos::panic("Unsupported Limine base revision");
    }

    call_global_constructors();
    myos::interrupts_init();
    memory_init();

    if (framebuffer_request.response == nullptr ||
        framebuffer_request.response->framebuffer_count == 0) {
        myos::panic("No framebuffer from Limine");
    }

    for (uint64_t i = 0; i < framebuffer_request.response->framebuffer_count; ++i) {
        limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[i];
        if (myos::desktop_init(framebuffer)) {
            myos::timer_start(hhdm_request.response->offset);
            myos::clock_init(myos::smbios_firmware_mhz(smbios_request.response,
                memmap_request.response, hhdm_request.response->offset));
            myos::exception_selftest(cmdline_request.response ? cmdline_request.response->cmdline : nullptr);
            myos::serial_write_line("MYOS: boot ok v0.3.1");
            myos::desktop_run();
        }
    }
    myos::panic("Unsupported framebuffer");
}

