#include <myos/framebuffer.hpp>
#include <myos/runtime.hpp>
#include <myos/serial.hpp>

extern "C" [[noreturn]] void myos_halt_forever();
namespace {
myos::Canvas *screen = nullptr;
bool panicking = false;
void hex(uint64_t value, char text[19]) {
    text[0] = '0'; text[1] = 'x';
    for (int i = 0; i < 16; ++i) { text[2 + i] = "0123456789abcdef"[(value >> (60 - 4 * i)) & 15]; }
    text[18] = 0;
}
void detail(const char *key, uint64_t value, int y) {
    char text[19];
    hex(value, text);
    myos::serial_write(key); myos::serial_write("="); myos::serial_write_line(text);
    if (screen) {
        screen->text(key, 40, y, 0xbfc9c5);
        screen->text(text, 160, y, 0xf5f8f6);
    }
}
}
namespace myos {
void panic_attach(Canvas *canvas) { screen = canvas; }
[[noreturn]] void panic(const char *reason, const InterruptFrame *frame, uint64_t fault_address) {
    asm volatile("cli" ::: "memory");
    if (panicking) { serial_write_line("MYOS: recursive panic"); myos_halt_forever(); }
    panicking = true;
    serial_write("MYOS: PANIC "); serial_write_line(reason);
    if (screen) {
        screen->rect({0, 0, screen->width(), screen->height()}, 0x252b29);
        screen->rect({0, 0, screen->width(), 12}, 0xc85b66);
        screen->text("ShawnOS", 40, 48, 0xf5f8f6, 3);
        screen->text("Kernel stopped", 40, 102, 0xe998a0, 3);
        screen->text(reason, 40, 150, 0xf5f8f6, 2, screen->width() - 80);
    }
    if (frame) {
        detail("Vector", frame->vector, 208);
        detail("Error", frame->error, 240);
        detail("RIP", frame->rip, 272);
        detail("RSP", frame->rsp, 304);
        if (frame->vector == 14) { detail("CR2", fault_address, 336); }
    }
    if (screen) {
        screen->text("Restart to begin a new session.", 40, screen->height() - 64, 0xbfc9c5);
        screen->present();
    }
    serial_write_line("MYOS: panic halted");
    myos_halt_forever();
}
}
