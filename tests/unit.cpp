#include <cassert>
#include <cstdio>
#include <vector>
#include <myos/framebuffer.hpp>
#include <myos/input.hpp>
#include <myos/text_buffer.hpp>
#include <myos/memory.hpp>

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
int main() {
    keyboard_tests(); mouse_tests(); text_tests();
    graphics_tests(640, 480); graphics_tests(1024, 768); graphics_tests(1920, 1200);
    memory_tests();
    std::puts("Host tests passed: input, text, graphics, and physical frame allocation.");
}
