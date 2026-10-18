#include <cassert>
#include <cstdio>
#include <vector>
#include <myos/framebuffer.hpp>
#include <myos/input.hpp>
#include <myos/text_buffer.hpp>
#include <myos/memory.hpp>
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
    limine_memmap_entry low = {0, 0x3000, LIMINE_MEMMAP_USABLE};
    limine_memmap_entry reserved = {0x3000, 0x1000, LIMINE_MEMMAP_EXECUTABLE_AND_MODULES};
    limine_memmap_entry usable = {0x4001, 0x3fff, LIMINE_MEMMAP_USABLE};
    limine_memmap_entry high = {FrameAllocator::LIMIT, 0x2000, LIMINE_MEMMAP_USABLE};
    limine_memmap_entry *map[] = {&low, &reserved, &usable, &high};
    assert(frames.init(map, 4));
    assert(frames.usable_bytes() == 0x3000 + 0x3fff + 0x2000);
    assert(frames.detected_bytes() == 0x3000 + 0x1000 + 0x3fff + 0x2000);
    assert(frames.managed_frames() == 5 && frames.free_frames() == 5);
    std::vector<uint64_t> allocated;
    while (uint64_t address = frames.allocate()) {
        assert(address % 4096 == 0 && address < FrameAllocator::LIMIT);
        assert(address != 0x3000 && address != 0x4000);
        for (uint64_t previous : allocated) { assert(previous != address); }
        allocated.push_back(address);
    }
    assert(allocated.size() == 5 && !frames.allocate() && frames.free_frames() == 0);
    assert(!frames.release(0) && !frames.release(0x3000) && !frames.release(0x4000));
    assert(!frames.release(allocated[0] + 1) && !frames.release(FrameAllocator::LIMIT));
    assert(frames.release(allocated[0]) && !frames.release(allocated[0]));
    assert(frames.allocate() == allocated[0] && frames.free_frames() == 0);
    for (uint64_t address : allocated) { assert(frames.release(address)); }
    assert(frames.free_frames() == frames.managed_frames());
    limine_memmap_entry overlap = {0x2000, 0x2000, LIMINE_MEMMAP_RESERVED};
    limine_memmap_entry *bad[] = {&low, &overlap};
    assert(!frames.init(bad, 2) && !frames.allocate());
    overlap = {UINT64_MAX - 2, 4, LIMINE_MEMMAP_USABLE};
    assert(!frames.init(bad, 2) && !frames.allocate());
    limine_memmap_entry *null_map[] = {nullptr};
    assert(!frames.init(nullptr, 1) && !frames.init(null_map, 1) && !frames.init(map, 0));
    assert(!frames.init(map, 4097));
    limine_memmap_entry *unsorted[] = {&usable, &low};
    assert(!frames.init(unsorted, 2));
    limine_memmap_entry r1 = {0x1000, 0x1000, LIMINE_MEMMAP_RESERVED};
    limine_memmap_entry r2 = {0x1800, 0x1000, LIMINE_MEMMAP_RESERVED_MAPPED};
    limine_memmap_entry *reserved_overlap[] = {&r1, &r2, &usable};
    assert(frames.init(reserved_overlap, 3) && frames.managed_frames() == 3);
    limine_memmap_entry reclaimable = {0x1000, 0x1000, LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE};
    limine_memmap_entry *boot_memory[] = {&reclaimable};
    assert(!frames.init(boot_memory, 1) && !frames.allocate());
    limine_memmap_entry boundary = {FrameAllocator::LIMIT - 4096, 8192, LIMINE_MEMMAP_USABLE};
    limine_memmap_entry *edge[] = {&boundary};
    assert(frames.init(edge, 1) && frames.managed_frames() == 1);
    assert(frames.allocate() == FrameAllocator::LIMIT - 4096 && !frames.allocate());
    assert(frames.release(FrameAllocator::LIMIT - 4096));
    low = {0, 4096, LIMINE_MEMMAP_USABLE};
    assert(!frames.init(map, 1) && !frames.allocate());
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
    cpu_tests();
    smbios_tests();
    keyboard_tests(); mouse_tests(); text_tests();
    graphics_tests(640, 480); graphics_tests(1024, 768); graphics_tests(1920, 1200);
    memory_tests();
    std::puts("Host tests passed: CPU reporting, input, text, graphics, and physical frame allocation.");
}
