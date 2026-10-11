#include <myos/input.hpp>

extern "C" void myos_outb(uint16_t port, uint8_t value);
extern "C" uint8_t myos_inb(uint16_t port);

namespace {
myos::KeyboardDecoder keyboard;
myos::MouseDecoder mouse;

bool ready_to_write() {
    for (int i = 0; i < 100000; ++i) {
        if ((myos_inb(0x64) & 2) == 0) { return true; }
        asm volatile("pause");
    }
    return false;
}
bool command(uint8_t value) {
    if (!ready_to_write()) { return false; }
    myos_outb(0x64, value);
    return true;
}
bool data(uint8_t value) {
    if (!ready_to_write()) { return false; }
    myos_outb(0x60, value);
    return true;
}
bool read(uint8_t &value, bool auxiliary) {
    for (int i = 0; i < 100000; ++i) {
        const uint8_t status = myos_inb(0x64);
        if ((status & 1) != 0) {
            value = myos_inb(0x60);
            if ((status & 0xc0) == 0 && ((status & 0x20) != 0) == auxiliary) {
                return true;
            }
        }
        asm volatile("pause");
    }
    return false;
}
bool device_command(uint8_t value, bool auxiliary) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        if ((auxiliary && !command(0xd4)) || !data(value)) { return false; }
        uint8_t response = 0;
        if (!read(response, auxiliary)) { return false; }
        if (response == 0xfa) { return true; }
        if (response != 0xfe) { return false; }
    }
    return false;
}
}

namespace myos {
InputDevices input_init() {
    // IRQs stay masked until the kernel has an IDT. Poll both controller ports.
    asm volatile("cli");
    myos_outb(0x21, 0xff);
    myos_outb(0xa1, 0xff);
    if (!command(0xad) || !command(0xa7)) { return {}; }
    for (int i = 0; i < 256 && (myos_inb(0x64) & 1); ++i) { myos_inb(0x60); }
    uint8_t config = 0;
    if (!command(0x20) || !read(config, false)) { return {}; }
    // Translate the keyboard's default set 2 to set 1; disable both IRQ bits.
    config = static_cast<uint8_t>((config | 0x40) & ~0x33);
    if (!command(0x60) || !data(config)) { return {}; }
    if (!command(0xae) || !command(0xa8)) { return {}; }
    const bool has_keyboard = device_command(0xf6, false) && device_command(0xf4, false);
    // A reset, not F6 defaults, restores three-byte packets after a wheel probe.
    uint8_t self_test = 0, mouse_id = 0;
    const bool has_mouse = device_command(0xff, true) &&
        read(self_test, true) && self_test == 0xaa &&
        read(mouse_id, true) && mouse_id == 0 && device_command(0xf4, true);
    return {has_keyboard, has_mouse};
}

bool input_poll(InputEvent &event) {
    for (int i = 0; i < 32; ++i) {
        const uint8_t status = myos_inb(0x64);
        if ((status & 1) == 0) { return false; }
        const uint8_t value = myos_inb(0x60);
        if ((status & 0xc0) != 0) { continue; }
        if ((status & 0x20) != 0 ? mouse.feed(value, event) : keyboard.feed(value, event)) {
            return true;
        }
    }
    return false;
}
}
