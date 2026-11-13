#include <cassert>
#include <cstdio>
#include <vector>
#include <myos/framebuffer.hpp>
#include <myos/input.hpp>
#include <myos/text_buffer.hpp>
#include <myos/memory.hpp>
#include <myos/memory_format.hpp>
#include <myos/uptime_format.hpp>
#include <myos/cpu.hpp>
#include <myos/smbios.hpp>
#include <cstring>

using namespace myos;

void keyboard_tests() {
    KeyboardDecoder decoder;
    InputEvent event;
    assert(decoder.feed(0x1e, event) && event.character == 'a');
    assert(!decoder.feed(0x9e, event));
    assert(!decoder.feed(0x2a, event));
    assert(decoder.feed(0x1e, event) && event.character == 'A');
    assert(decoder.feed(0x02, event) && event.character == '!');
    assert(!decoder.feed(0x36, event));
    assert(!decoder.feed(0xaa, event));
    assert(decoder.feed(0x1e, event) && event.character == 'A');
    assert(!decoder.feed(0xb6, event));
    assert(!decoder.feed(0x3a, event));
    assert(!decoder.feed(0x3a, event)); // Caps lock repeat must not toggle again.
    assert(!decoder.feed(0xba, event));
    assert(decoder.feed(0x1e, event) && event.character == 'A');
    decoder.feed(0x2a, event);
    assert(decoder.feed(0x1e, event) && event.character == 'a');
    decoder.feed(0xaa, event);
    assert(decoder.feed(0x02, event) && event.character == '1');
    assert(!decoder.feed(0xe0, event));
    assert(!decoder.feed(0x48, event));
    decoder.feed(0xe0, event);
    assert(decoder.feed(0x1c, event) && event.key == Key::Enter);
    decoder.feed(0xe1, event);
    for (uint8_t code : {0x1d, 0x45, 0xe1, 0x9d, 0xc5}) { assert(!decoder.feed(code, event)); }
    assert(decoder.feed(0x3c, event) && event.key == Key::F2);
    assert(decoder.feed(0x0e, event) && event.key == Key::Backspace);
    assert(!decoder.feed(0xff, event));
    const uint8_t codes[] = {0x10,0x1a,0x1b,0x27,0x28,0x29,0x2b,0x2c,0x33,0x34,0x35,0x39};
    const char expected[] = "Q[];'`\\Z,./ ";
    for (unsigned i = 0; i < sizeof(codes); ++i) {
        assert(decoder.feed(codes[i], event) && event.character == expected[i]);
    }
}
void mouse_tests() {
    MouseDecoder decoder;
    InputEvent event;
    assert(!decoder.feed(0, event));
    assert(!decoder.feed(0x09, event));
    assert(!decoder.feed(200, event));
    assert(decoder.feed(50, event) && event.mouse && event.left && event.dx == 200 && event.dy == -50);
    decoder.feed(0x38, event);
    decoder.feed(200, event);
    assert(decoder.feed(250, event) && !event.left && event.dx == -56 && event.dy == 6);
    decoder.feed(0xc9, event);
    decoder.feed(255, event);
    assert(decoder.feed(255, event) && event.left && event.dx == 0 && event.dy == 0);
}
void text_tests() {
    TextBuffer<4> text;
    assert(!text.backspace());
    assert(!text.append(0));
    assert(text.append('a') && text.append('\n') && text.append('b'));
    assert(!text.append('c') && text.size == 3 && text.data[3] == 0);
    assert(text.backspace() && text.size == 2 && text.data[2] == 0);
    text.clear();
    assert(text.size == 0 && text.data[0] == 0);
}
void graphics_tests(int width, int height) {
    constexpr uint32_t GUARD = 0xdeadbeef;
    const int stride = width + 8;
    std::vector<uint32_t> memory(stride * height + 2, GUARD);
    std::vector<uint32_t> pixels(width * height + 2, GUARD);
    limine_framebuffer fb = {};
    fb.address = memory.data() + 1;
    fb.width = width; fb.height = height; fb.pitch = stride * 4;
    fb.bpp = 32; fb.memory_model = LIMINE_FRAMEBUFFER_RGB;
    fb.red_mask_size = fb.green_mask_size = fb.blue_mask_size = 8;
    fb.red_mask_shift = 16; fb.green_mask_shift = 8; fb.blue_mask_shift = 0;
    Canvas canvas;
    assert(canvas.init(&fb, pixels.data() + 1));
    assert(canvas.color(0x123456) == 0x123456);
    canvas.rect({0, 0, width, height}, 0x123456);
    canvas.rect({-20, -10, 40, 30}, 0xabcdef);
    canvas.rect({width - 10, height - 10, 40, 30}, 0x987654);
    canvas.pixel(-1, 0, 0);
    canvas.pixel(width, height, 0);
    canvas.text("ABCDEFGHIJKLMNOPQRSTUVWXYZ", width - 6, height - 3, 0xffffff);
    canvas.text("lowercase", -8, -8, 0xffffff);
    canvas.present();
    assert(pixels.front() == GUARD && pixels.back() == GUARD);
    assert(memory.front() == GUARD && memory.back() == GUARD);
    for (int y = 0; y < height; ++y) {
        for (int x = width; x < stride; ++x) { assert(memory[1 + y * stride + x] == GUARD); }
    }
    fb.red_mask_shift = 0; fb.blue_mask_shift = 16;
    assert(canvas.init(&fb, pixels.data() + 1) && canvas.color(0x123456) == 0x563412);
    fb.red_mask_shift = 8;
    assert(!canvas.init(&fb, pixels.data() + 1));
    fb.red_mask_shift = 0; fb.red_mask_size = 33;
    assert(!canvas.init(&fb, pixels.data() + 1));
    fb.red_mask_size = 8; fb.pitch = width * 4 - 1;
    assert(!canvas.init(&fb, pixels.data() + 1));
    fb.pitch = stride * 4; fb.width = 639;
    assert(!canvas.init(&fb, pixels.data() + 1));
}
void memory_tests() {
    FrameAllocator frames;
    FrameStorage storage;
    std::vector<uint8_t> backing;
    const auto init = [&](limine_memmap_entry *const *entries, size_t count) {
        if (!FrameAllocator::storage_plan(entries, count, storage)) {
            assert(storage.address == 0 && storage.bytes == 0);
            return frames.init(entries, count, storage, nullptr);
        }
        backing.assign(storage.bytes + 2, 0xa5);
        const bool ok = frames.init(entries, count, storage, backing.data() + 1);
        assert(backing.front() == 0xa5 && backing.back() == 0xa5);
        return ok;
    };
    limine_memmap_entry low = {0, 0x3000, LIMINE_MEMMAP_USABLE};
    limine_memmap_entry reserved = {0x3000, 0x1000, LIMINE_MEMMAP_EXECUTABLE_AND_MODULES};
    limine_memmap_entry usable = {0x4001, 0x3fff, LIMINE_MEMMAP_USABLE};
    constexpr uint64_t four_gb = 1ull << 32;
    limine_memmap_entry high = {four_gb, 0x2000, LIMINE_MEMMAP_USABLE};
    limine_memmap_entry *map[] = {&low, &reserved, &usable, &high};
    assert(init(map, 4));
    assert(frames.usable_bytes() == 0x3000 + 0x3fff + 0x2000);
    assert(frames.detected_bytes() == 0x3000 + 0x1000 + 0x3fff + 0x2000);
    assert(frames.managed_frames() == 7 && frames.free_frames() == 6);
    assert(storage.address == 4096 && storage.bytes == 4096);
    assert(!frames.release(storage.address));
    assert(frames.allocate(four_gb + 1) == four_gb + 4096);
    assert(!frames.allocate(four_gb + 1) && frames.free_frames() == 5);
    assert(frames.release(four_gb + 4096));
    std::vector<uint64_t> allocated;
    while (uint64_t address = frames.allocate()) {
        assert(address % 4096 == 0 && address < four_gb + 8192);
        assert(address != storage.address);
        assert(address != 0x3000 && address != 0x4000);
        for (uint64_t previous : allocated) { assert(previous != address); }
        allocated.push_back(address);
    }
    assert(allocated.size() == 6 && !frames.allocate() && frames.free_frames() == 0);
    assert(!frames.release(0) && !frames.release(0x3000) && !frames.release(0x4000));
    assert(!frames.release(allocated[0] + 1) && !frames.release(four_gb + 8192));
    assert(frames.release(allocated[0]) && !frames.release(allocated[0]));
    assert(frames.allocate() == allocated[0] && frames.free_frames() == 0);
    for (uint64_t address : allocated) { assert(frames.release(address)); }
    assert(frames.free_frames() == frames.managed_frames() - 1);
    assert(backing.front() == 0xa5 && backing.back() == 0xa5);
    limine_memmap_entry overlap = {0x2000, 0x2000, LIMINE_MEMMAP_RESERVED};
    limine_memmap_entry *bad[] = {&low, &overlap};
    assert(!init(bad, 2) && !frames.allocate());
    overlap = {UINT64_MAX - 2, 4, LIMINE_MEMMAP_USABLE};
    assert(!init(bad, 2) && !frames.allocate());
    limine_memmap_entry *null_map[] = {nullptr};
    assert(!init(nullptr, 1) && !init(null_map, 1) && !init(map, 0));
    assert(!init(map, 4097));
    limine_memmap_entry *unsorted[] = {&usable, &low};
    assert(!init(unsorted, 2));
    limine_memmap_entry r1 = {0x1000, 0x1000, LIMINE_MEMMAP_RESERVED};
    limine_memmap_entry r2 = {0x1800, 0x1000, LIMINE_MEMMAP_RESERVED_MAPPED};
    limine_memmap_entry *reserved_overlap[] = {&r1, &r2, &usable};
    assert(init(reserved_overlap, 3) && frames.managed_frames() == 3 && frames.free_frames() == 2);
    limine_memmap_entry reclaimable = {0x1000, 0x1000, LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE};
    limine_memmap_entry *boot_memory[] = {&reclaimable};
    assert(!init(boot_memory, 1) && !frames.allocate());
    limine_memmap_entry boundary = {four_gb - 4096, 12288, LIMINE_MEMMAP_USABLE};
    limine_memmap_entry *edge[] = {&boundary};
    assert(init(edge, 1) && frames.managed_frames() == 3);
    assert(frames.allocate() == four_gb && frames.allocate() == four_gb + 4096 && !frames.allocate());
    assert(frames.release(four_gb) && !frames.release(four_gb - 4096));
    boundary = {UINT64_MAX - 16383, 16383, LIMINE_MEMMAP_USABLE};
    assert(init(edge, 1) && frames.managed_frames() == 3);
    assert(frames.highest_address() == UINT64_MAX - 4095);
    assert(frames.allocate(UINT64_MAX - 8191) == UINT64_MAX - 8191);
    assert(!frames.allocate(UINT64_MAX));
    assert(frames.release(UINT64_MAX - 8191));
    high = {1ull << 48, 8192, LIMINE_MEMMAP_USABLE};
    assert(init(map, 4) && storage.bytes == 4096);
    assert(frames.allocate(1ull << 48) == (1ull << 48));
    assert(frames.release(1ull << 48));
    assert(!frames.release((1ull << 48) - 4096));
    FrameStorage wrong = storage;
    ++wrong.bytes;
    assert(!frames.init(map, 4, wrong, backing.data() + 1) && !frames.allocate());
    wrong = storage; ++wrong.address;
    assert(!frames.init(map, 4, wrong, backing.data() + 1) && !frames.allocate());
    low = {0, 4096, LIMINE_MEMMAP_USABLE};
    assert(!init(map, 1) && !frames.allocate());

    uint32_t random = 0x51a7c0de;
    for (unsigned trial = 0; trial < 32; ++trial) {
        std::vector<limine_memmap_entry> entries;
        uint64_t base = 4096;
        for (unsigned n = 0; n < 40; ++n) {
            random = random * 1664525u + 1013904223u;
            const uint64_t length = (1 + random % 17) * 4096;
            entries.push_back({base, length, static_cast<uint64_t>(n % 3 == 1 ? LIMINE_MEMMAP_RESERVED : LIMINE_MEMMAP_USABLE)});
            base += length + (1ull << 30);
        }
        std::vector<limine_memmap_entry *> pointers;
        for (auto &entry : entries) { pointers.push_back(&entry); }
        assert(init(pointers.data(), pointers.size()));
        std::vector<uint64_t> expected;
        for (const auto &entry : entries) {
            if (entry.type != LIMINE_MEMMAP_USABLE) { continue; }
            for (uint64_t address = entry.base; address < entry.base + entry.length; address += 4096) {
                if (address < storage.address || address - storage.address >= storage.bytes) { expected.push_back(address); }
            }
        }
        assert(frames.free_frames() == expected.size());
        for (uint64_t address : expected) { assert(frames.allocate() == address); }
        assert(!frames.allocate());
        for (auto it = expected.rbegin(); it != expected.rend(); ++it) { assert(frames.release(*it)); }
        const size_t middle = expected.size() / 2;
        assert(frames.allocate(expected[middle] + 1) == expected[middle + 1]);
        assert(frames.release(expected[middle + 1]));
        assert(frames.free_frames() == expected.size());
        const uint64_t before = frames.free_frames();
        for (const auto &entry : entries) { assert(!frames.release(entry.base + entry.length)); }
        assert(frames.free_frames() == before);
        assert(backing.front() == 0xa5 && backing.back() == 0xa5);
    }

    // Exhaust every page without allocating the represented RAM on the host.
    for (uint64_t mb : {64, 4096, 16384, 32768}) {
        low = {0, mb * 1024 * 1024, LIMINE_MEMMAP_USABLE};
        assert(init(map, 1));
        const uint64_t total = low.length / 4096;
        assert(frames.managed_frames() == total - 1);
        const uint64_t first = 1 + storage.bytes / 4096;
        assert(frames.free_frames() == total - first);
        assert(!frames.allocate(low.length));
        for (uint64_t n = first; n < total; ++n) { assert(frames.allocate() == n * 4096); }
        assert(!frames.free_frames() && !frames.allocate());
        for (uint64_t n = total; n-- > first;) { assert(frames.release(n * 4096)); }
        assert(frames.free_frames() == total - first);
        for (uint64_t n = 1; n < first; ++n) { assert(!frames.release(n * 4096)); }
        assert(frames.allocate() == first * 4096);
        assert(backing.front() == 0xa5 && backing.back() == 0xa5);
    }
}
void memory_format_tests() {
    const struct { uint64_t bytes; const char *expected; } cases[] = {
        {0, "0.00 MB"}, {1, "0.00 MB"}, {5242, "0.00 MB"}, {5243, "0.01 MB"},
        {1000000, "0.95 MB"}, {1043333, "0.99 MB"}, {1043334, "1.00 MB"},
        {1048576, "1.00 MB"}, {64000000, "61.04 MB"},
        {64ull * 1024 * 1024, "64.00 MB"}, {1000000000, "953.67 MB"},
        {1073736581, "1023.99 MB"}, {1073736582, "1024.00 MB"},
        {(1ull << 30) - 1, "1024.00 MB"}, {1ull << 30, "1.00 GB"},
        {1079110533, "1.00 GB"}, {1079110534, "1.01 GB"},
        {1536ull * 1024 * 1024, "1.50 GB"},
        {4096ull * 1024 * 1024, "4.00 GB"},
        {8192ull * 1024 * 1024, "8.00 GB"},
        {1000000000000ull, "931.32 GB"},
        {UINT64_MAX, "17179869184.00 GB"},
    };
    for (const auto &test : cases) {
        const auto text = memory_size_text(test.bytes);
        assert(std::strcmp(text.data, test.expected) == 0);
        assert(text.size == std::strlen(test.expected) && text.data[text.size] == 0);
        assert(text_width(text.data) <= 260);
    }
}

void uptime_format_tests() {
    const struct { uint64_t seconds; const char *expected; } cases[] = {
        {0, "0s"}, {1, "1s"}, {59, "59s"}, {60, "1m 0s"},
        {3599, "59m 59s"}, {3600, "1h 0m 0s"}, {86399, "23h 59m 59s"},
        {86400, "1d 0h 0m 0s"}, {45123, "12h 32m 3s"}, {90061, "1d 1h 1m 1s"},
        {365ull * 86400, "365d 0h 0m 0s"},
        {UINT64_MAX, "213503982334601d 7h 0m 15s"},
    };
    for (const auto &test : cases) {
        const auto text = uptime_text(test.seconds);
        assert(std::strcmp(text.data, test.expected) == 0);
        assert(text.size == std::strlen(test.expected) && text.data[text.size] == 0);
        assert(text_width(text.data, 1) <= 260); // About can reduce the scale for long uptimes.
        assert(text_width("Uptime: ") + text_width(text.data) <= 488); // Narrowest Terminal.
    }
}

void cpu_tests() {
    using namespace myos;
    const CpuIdRegisters intel{0x16, 0x756e6547, 0x6c65746e, 0x49656e69};
    assert(has_cpu_base_frequency(intel));
    assert(reported_cpu_base_mhz(intel, 2400) == 2400);
    assert(reported_cpu_base_mhz(intel, 0) == 0);
    assert(reported_cpu_base_mhz(intel, 0xabcd1234) == 0x1234);
    assert(reported_cpu_base_mhz(intel, 0xffff0000) == 0);
    auto unsupported = intel;
    unsupported.eax = 0x15;
    assert(!has_cpu_base_frequency(unsupported));
    assert(reported_cpu_base_mhz(unsupported, 2400) == 0);
    unsupported.eax = 0;
    assert(reported_cpu_base_mhz(unsupported, 2400) == 0);
    const CpuIdRegisters amd{0x16, 0x68747541, 0x444d4163, 0x69746e65};
    assert(!has_cpu_base_frequency(amd));
    assert(reported_cpu_base_mhz(amd, 2400) == 0);
    assert(reported_cpu_base_mhz(CpuIdRegisters{}, 2400) == 0);
    assert(select_cpu_frequency(2400, 1800, 800).source == CpuFrequencySource::CpuidBase);
    assert(select_cpu_frequency(0, 1800, 800).source == CpuFrequencySource::IntelBrand);
    const auto firmware = select_cpu_frequency(0, 0, 800);
    assert(firmware.mhz == 800 && firmware.source == CpuFrequencySource::FirmwareBoot);
    assert(std::strcmp(firmware.label(), "CPU boot speed") == 0);
    assert(select_cpu_frequency(0, 0, 0).source == CpuFrequencySource::Unavailable);

    const struct {
        const char *brand;
        uint64_t mhz;
    } brands[] = {
        {"Example CPU @ 800MHz", 800},
        {"Example CPU @ 0.80GHz", 800},
        {"Example CPU @ 999MHz", 999},
        {"Example CPU @ 1GHz", 1000},
        {"Example CPU @ 1.001GHz", 1001},
        {"Example CPU @ 2.60GHz", 2600},
        {"Different CPU @ 4.10 GHz   ", 4100},
        {"Example CPU @2.4GHz", 2400},
        {"Example CPU @ 800.000MHz", 800},
        {"Example CPU @ 18446744073709551615MHz", UINT64_MAX},
        {"Example CPU @ 18446744073709551.615GHz", UINT64_MAX},
        {"", 0}, {"Example CPU 2600", 0}, {"Example CPU 2.60GHz", 0},
        {"Example CPU @ 0GHz", 0}, {"Example CPU @ -2.60GHz", 0},
        {"Example CPU @ .8GHz", 0}, {"Example CPU @ 2.GHz", 0},
        {"Example CPU @ 2.6000GHz", 0}, {"Example CPU @ 800.5MHz", 0},
        {"Example CPU @ 2.60kHz", 0}, {"Example CPU @ 2.60GHz extra", 0},
        {"Example CPU @ 2GHz @ 3GHz", 0},
        {"Example CPU @ 18446744073709551616MHz", 0},
        {"Example CPU @ 18446744073709552GHz", 0},
        {"Example CPU @ 18446744073709551.616GHz", 0},
    };
    for (const auto &test : brands) {
        assert(cpu_brand_base_mhz(intel, test.brand, std::strlen(test.brand)) == test.mhz);
        assert(cpu_brand_base_mhz(amd, test.brand, std::strlen(test.brand)) == 0);
        assert(cpu_brand_base_mhz(CpuIdRegisters{}, test.brand, std::strlen(test.brand)) == 0);
    }
    assert(cpu_brand_base_mhz(intel, nullptr, 48) == 0);
    const char suffix[] = {'@', '2', '.', '6', '0', 'G', 'H', 'z'};
    assert(cpu_brand_base_mhz(intel, suffix, sizeof(suffix)) == 2600);
    assert(cpu_brand_base_mhz(intel, suffix, sizeof(suffix) - 1) == 0);
    assert(cpu_brand_base_mhz(intel, suffix, 0) == 0);
    char full_brand[CPU_BRAND_BYTES];
    std::memset(full_brand, ' ', sizeof(full_brand));
    std::memcpy(full_brand + sizeof(full_brand) - sizeof(suffix), suffix, sizeof(suffix));
    assert(cpu_brand_base_mhz(intel, full_brand, sizeof(full_brand)) == 2600);
    const char hidden[] = {'C', 'P', 'U', 0, '@', '2', 'G', 'H', 'z'};
    assert(cpu_brand_base_mhz(intel, hidden, sizeof(hidden)) == 0);
    auto brand_only = intel;
    brand_only.eax = 0x15;
    assert(cpu_brand_base_mhz(brand_only, suffix, sizeof(suffix)) == 2600);

    const struct {
        uint64_t mhz;
        const char *expected;
    } cases[] = {
        {0, "Unavailable"}, {1, "1 MHz"}, {800, "800 MHz"},
        {999, "999 MHz"}, {1000, "1.00 GHz"}, {1004, "1.00 GHz"},
        {1005, "1.01 GHz"}, {1994, "1.99 GHz"}, {1995, "2.00 GHz"},
        {1999, "2.00 GHz"}, {2400, "2.40 GHz"}, {4100, "4.10 GHz"},
        {65535, "65.54 GHz"},
        {UINT64_MAX, "18446744073709551.62 GHz"},
    };
    for (const auto &test : cases) {
        const auto text = cpu_frequency_text(test.mhz);
        assert(std::strcmp(text.data, test.expected) == 0);
        assert(text.size == std::strlen(test.expected));
        assert(text.size < sizeof(text.data));
    }
}

void smbios_tests() {
    const auto put = [](std::vector<uint8_t> &v, size_t at, uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) v[at + i] = static_cast<uint8_t>(value >> (i * 8));
    };
    const auto sum_fix = [](std::vector<uint8_t> &v, size_t first, size_t size, size_t check) {
        v[check] = 0;
        uint8_t sum = 0;
        for (size_t i = first; i < first + size; ++i) sum += v[i];
        v[check] = static_cast<uint8_t>(0 - sum);
    };
    const auto processor = [&put](uint16_t speed) {
        std::vector<uint8_t> v(28);
        v[0] = 4; v[1] = 26; v[5] = 3; v[24] = 0x41;
        put(v, 20, 5000, 2); // System maximum is deliberately not the boot speed.
        put(v, 22, speed, 2);
        return v;
    };
    const std::vector<uint8_t> end{127, 4, 0xff, 0xff, 0, 0};
    auto table = processor(800);
    table.insert(table.end(), end.begin(), end.end());
    assert(smbios_cpu_boot_mhz(table.data(), table.size()) == 800);
    assert(smbios_cpu_boot_mhz(table.data(), table.size(), 2) == 800);
    for (size_t n = 0; n < table.size(); ++n) assert(!smbios_cpu_boot_mhz(table.data(), n));
    assert(!smbios_cpu_boot_mhz(nullptr, table.size()));
    assert(!smbios_cpu_boot_mhz(table.data(), SMBIOS_TABLE_LIMIT + 1));
    assert(!smbios_cpu_boot_mhz(table.data(), table.size(), 3));
    auto changed = table;
    changed[1] = 0;
    assert(!smbios_cpu_boot_mhz(changed.data(), changed.size()));
    changed = table; changed[1] = 255;
    assert(!smbios_cpu_boot_mhz(changed.data(), changed.size()));
    changed = table; changed[24] = 0;
    assert(!smbios_cpu_boot_mhz(changed.data(), changed.size()));
    changed[24] = 0x42;
    assert(!smbios_cpu_boot_mhz(changed.data(), changed.size()));
    for (uint16_t speed : {0, 999, 1000, 3600, 65535}) {
        changed = table; put(changed, 22, speed, 2);
        assert(smbios_cpu_boot_mhz(changed.data(), changed.size()) == (speed == 65535 ? 0 : speed));
    }
    for (uint16_t speed : {0, 800, 1000}) {
        changed = processor(800);
        const auto second = processor(speed);
        changed.insert(changed.end(), second.begin(), second.end());
        changed.insert(changed.end(), end.begin(), end.end());
        assert(smbios_cpu_boot_mhz(changed.data(), changed.size()) == (speed == 800 ? 800 : 0));
    }
    auto no_strings_end = processor(800);
    no_strings_end[26] = 'x'; no_strings_end[27] = 'y';
    assert(!smbios_cpu_boot_mhz(no_strings_end.data(), no_strings_end.size()));
    const auto legacy_table = processor(1800);
    assert(smbios_cpu_boot_mhz(legacy_table.data(), legacy_table.size(), 1) == 1800);

    std::vector<uint8_t> entry32(31);
    std::memcpy(entry32.data(), "_SM_", 4); entry32[5] = 31; entry32[6] = 2; entry32[7] = 8;
    std::memcpy(entry32.data() + 16, "_DMI_", 5);
    put(entry32, 22, table.size(), 2); put(entry32, 24, 0x10000, 4); put(entry32, 28, 2, 2);
    sum_fix(entry32, 16, 15, 21); sum_fix(entry32, 0, 31, 4);
    SmbiosTable description;
    assert(smbios_entry(entry32.data(), entry32.size(), description));
    assert(description.address == 0x10000 && description.exact_size && description.count == 2);
    for (size_t n = 0; n < 31; ++n) assert(!smbios_entry(entry32.data(), n, description));
    changed = entry32; changed[5] = 30; changed[7] = 1; sum_fix(changed, 0, 30, 4);
    assert(smbios_entry(changed.data(), changed.size(), description));
    assert(!smbios_entry(changed.data(), 30, description));
    changed[7] = 2; sum_fix(changed, 0, 30, 4);
    assert(!smbios_entry(changed.data(), changed.size(), description));
    changed = entry32; changed[21] ^= 1; sum_fix(changed, 0, 31, 4);
    assert(!smbios_entry(changed.data(), changed.size(), description));
    changed = entry32; changed[4] ^= 1;
    assert(!smbios_entry(changed.data(), changed.size(), description));

    std::vector<uint8_t> entry64(24);
    std::memcpy(entry64.data(), "_SM3_", 5); entry64[6] = 24; entry64[7] = 3; entry64[10] = 1;
    put(entry64, 12, table.size(), 4); put(entry64, 16, 0x100000000ull, 8);
    sum_fix(entry64, 0, 24, 5);
    assert(smbios_entry(entry64.data(), entry64.size(), description));
    assert(description.address == 0x100000000ull && !description.exact_size);
    for (size_t n = 0; n < 24; ++n) assert(!smbios_entry(entry64.data(), n, description));
    changed = entry64; put(changed, 16, UINT64_MAX - 1, 8); sum_fix(changed, 0, 24, 5);
    assert(!smbios_entry(changed.data(), changed.size(), description));
    changed = entry64; put(changed, 12, SMBIOS_TABLE_LIMIT + 1, 4); sum_fix(changed, 0, 24, 5);
    assert(!smbios_entry(changed.data(), changed.size(), description));
    assert(!smbios_entry(nullptr, 24, description));

    limine_memmap_entry one{0x10000, 0x1000, LIMINE_MEMMAP_RESERVED_MAPPED};
    limine_memmap_entry two{0x11000, 0x1000, LIMINE_MEMMAP_RESERVED_MAPPED};
    limine_memmap_entry *regions[]{&one, &two};
    assert(smbios_mapped_size(0x10ff0, 32, regions, 2) == 32);
    two.base = 0x12000;
    assert(smbios_mapped_size(0x10ff0, 32, regions, 2) == 16);
    one.type = LIMINE_MEMMAP_RESERVED;
    assert(!smbios_mapped_size(0x10000, 32, regions, 2));
    assert(!smbios_mapped_size(UINT64_MAX, 32, regions, 2));
    assert(!smbios_mapped_size(0x10000, 32, nullptr, 2));

    std::vector<uint8_t> memory(512);
    const uint64_t address = reinterpret_cast<uint64_t>(memory.data());
    put(entry64, 16, address + 256, 8); sum_fix(entry64, 0, 24, 5);
    std::memcpy(memory.data(), entry64.data(), entry64.size());
    std::memcpy(memory.data() + 256, table.data(), table.size());
    one = {address, memory.size(), LIMINE_MEMMAP_RESERVED_MAPPED};
    limine_memmap_response map{0, 1, regions};
    limine_smbios_response response{0, nullptr, memory.data()};
    assert(smbios_firmware_mhz(&response, &map, 0) == 800);
    one.type = LIMINE_MEMMAP_RESERVED;
    assert(!smbios_firmware_mhz(&response, &map, 0));
    assert(!smbios_firmware_mhz(nullptr, &map, 0));
    assert(!smbios_firmware_mhz(&response, nullptr, 0));

    uint32_t random = 1;
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        std::vector<uint8_t> malformed(iteration % 160);
        for (auto &byte : malformed) { random = random * 1664525u + 1013904223u; byte = random >> 24; }
        smbios_entry(malformed.data(), malformed.size(), description);
        smbios_cpu_boot_mhz(malformed.data(), malformed.size());
    }
}

int main() {
    memory_format_tests();
    uptime_format_tests();
    cpu_tests();
    smbios_tests();
    keyboard_tests(); mouse_tests(); text_tests();
    graphics_tests(640, 480); graphics_tests(1024, 768); graphics_tests(1920, 1200);
    memory_tests();
    std::puts("Host tests passed: memory display, uptime, CPU reporting, input, text, graphics, and physical frame allocation.");
}
