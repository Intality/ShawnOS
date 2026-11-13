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

void serial_number(uint64_t value) {
    char text[21];
    unsigned end = 20;
    text[end] = 0;
    do { text[--end] = static_cast<char>('0' + value % 10); value /= 10; } while (value);
    myos::serial_write(text + end);
}

volatile uint64_t *frame_mapping(uint64_t address, uint64_t offset) {
    if (!address || address > UINT64_MAX - (myos::FrameAllocator::PAGE_SIZE - 1) ||
        offset > UINT64_MAX - address - (myos::FrameAllocator::PAGE_SIZE - 1)) {
        myos::panic("Frame mapping overflow");
    }
    return reinterpret_cast<volatile uint64_t *>(address + offset);
}

void check_frame(uint64_t address, uint64_t offset) {
    auto *data = frame_mapping(address, offset);
    const uint64_t pattern = address ^ 0x51a7c0de00000000ull;
    for (unsigned i = 0; i < 512; ++i) { data[i] = pattern + i; }
    for (unsigned i = 0; i < 512; ++i) {
        if (data[i] != pattern + i) { myos::panic("High frame write/read failed"); }
    }
    for (unsigned i = 0; i < 512; ++i) { data[i] = 0; }
}

bool capacity_test_requested(const char *command_line) {
    constexpr char argument[] = "selftest=memory";
    if (!command_line) { return false; }
    for (size_t i = 0; i < sizeof(argument); ++i) {
        if (command_line[i] != argument[i]) { return false; }
    }
    return true;
}

void capacity_test(limine_memmap_response &map, myos::FrameAllocator &frames) {
    constexpr uint64_t page = myos::FrameAllocator::PAGE_SIZE;
    const auto storage = frames.storage();
    const uint64_t before = frames.free_frames();
    uint64_t allocated = 0, high = 0;
    while (uint64_t address = frames.allocate()) {
        if (++allocated > before || address % page) { myos::panic("Capacity allocation failed"); }
        if (address >= (1ull << 32)) { ++high; }
    }
    if (allocated != before || frames.free_frames()) { myos::panic("Incomplete RAM coverage"); }
    uint64_t released = 0;
    for (size_t n = 0; n < map.entry_count; ++n) {
        const auto &entry = *map.entries[n];
        if (entry.type != LIMINE_MEMMAP_USABLE) { continue; }
        uint64_t first = entry.base / page + (entry.base % page != 0);
        if (!first) { first = 1; }
        const uint64_t stop = (entry.base + entry.length) / page;
        for (uint64_t index = first; index < stop; ++index) {
            const uint64_t address = index * page;
            const bool metadata = address >= storage.address && address - storage.address < storage.bytes;
            if (frames.release(address) == metadata) { myos::panic("Capacity release failed"); }
            if (!metadata) { ++released; }
        }
    }
    if (released != allocated || frames.free_frames() != before) { myos::panic("Capacity recovery failed"); }
    myos::serial_write("MYOS: memory capacity passed allocated="); serial_number(allocated);
    myos::serial_write(" high="); serial_number(high);
    myos::serial_write(" released="); serial_number(released);
    myos::serial_write_line("");
}

void memory_init(const char *command_line) {
    auto *map = memmap_request.response;
    auto *direct_map = hhdm_request.response;
    auto &frames = myos::physical_frames();
    myos::FrameStorage storage;
    if (!map || !direct_map || !myos::FrameAllocator::storage_plan(map->entries, map->entry_count, storage)) {
        myos::panic("Invalid or unsupported memory map");
    }
    if (storage.bytes - 1 > UINT64_MAX - storage.address ||
        direct_map->offset > UINT64_MAX - storage.address - (storage.bytes - 1) ||
        !frames.init(map->entries, map->entry_count, storage,
                     reinterpret_cast<void *>(storage.address + direct_map->offset))) {
        myos::panic("Frame metadata mapping failed");
    }
    myos::serial_write_line("MYOS: memory map ready");
    myos::serial_write("MYOS: frame allocator ready managed="); serial_number(frames.managed_frames());
    myos::serial_write(" metadata="); serial_number(storage.bytes / myos::FrameAllocator::PAGE_SIZE);
    myos::serial_write(" highest="); serial_number(frames.highest_address());
    myos::serial_write_line("");
    const uint64_t before = frames.free_frames();
    const uint64_t first = frames.allocate(), second = frames.allocate();
    if (!first || !second || first == second) { myos::panic("Frame allocation failed"); }
    auto *a = frame_mapping(first, direct_map->offset);
    auto *b = frame_mapping(second, direct_map->offset);
    for (unsigned i = 0; i < 512; ++i) { a[i] = 0x51a7c0de00000000ull + i; b[i] = ~a[i]; }
    for (unsigned i = 0; i < 512; ++i) {
        if (a[i] != 0x51a7c0de00000000ull + i || b[i] != ~a[i]) { myos::panic("Frame write/read failed"); }
    }
    for (unsigned i = 0; i < 512; ++i) { a[i] = 0; b[i] = 0; }
    if (!frames.release(first) || !frames.release(second) || frames.free_frames() != before) {
        myos::panic("Frame release failed");
    }
    const uint64_t first_high = frames.allocate(1ull << 32);
    if (first_high) {
        check_frame(first_high, direct_map->offset);
        if (!frames.release(first_high)) { myos::panic("High frame release failed"); }
        const uint64_t last_high = frames.allocate(frames.highest_address() - myos::FrameAllocator::PAGE_SIZE);
        if (!last_high) { myos::panic("Last frame allocation failed"); }
        check_frame(last_high, direct_map->offset);
        if (!frames.release(last_high) || frames.free_frames() != before) { myos::panic("High frame recovery failed"); }
        myos::serial_write("MYOS: high memory write/read passed first="); serial_number(first_high);
        myos::serial_write(" last="); serial_number(last_high);
        myos::serial_write_line("");
    }
    myos::serial_write_line("MYOS: frame allocator self-test passed");
    if (capacity_test_requested(command_line)) { capacity_test(*map, frames); }
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
    memory_init(cmdline_request.response ? cmdline_request.response->cmdline : nullptr);

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

