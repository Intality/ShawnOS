#include <stdint.h>

#include <myos/serial.hpp>

extern "C" void myos_outb(uint16_t port, uint8_t value);
extern "C" uint8_t myos_inb(uint16_t port);

namespace {

constexpr uint16_t COM1 = 0x3f8;

bool transmit_empty() {
    return (myos_inb(COM1 + 5) & 0x20) != 0;
}

void write_char(char c) {
    for (uint32_t spin = 0; spin < 100000 && !transmit_empty(); ++spin) {
    }
    myos_outb(COM1, static_cast<uint8_t>(c));
}

} // namespace

namespace myos {

void serial_init() {
    myos_outb(COM1 + 1, 0x00);
    myos_outb(COM1 + 3, 0x80);
    myos_outb(COM1 + 0, 0x03);
    myos_outb(COM1 + 1, 0x00);
    myos_outb(COM1 + 3, 0x03);
    myos_outb(COM1 + 2, 0xc7);
    myos_outb(COM1 + 4, 0x0b);
}

void serial_write(const char *text) {
    if (text == nullptr) {
        return;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        write_char(*p);
    }
}

void serial_write_line(const char *text) {
    serial_write(text);
    serial_write("\r\n");
}

} // namespace myos

