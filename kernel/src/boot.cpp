#include <stddef.h>
#include <stdint.h>

#include <limine.h>
#include <myos/framebuffer.hpp>
#include <myos/serial.hpp>

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

__attribute__((used, section(".limine_requests_end")))
volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

void call_global_constructors() {
    for (size_t i = 0; &__init_array[i] != __init_array_end; ++i) {
        __init_array[i]();
    }
}

} // namespace

extern "C" void kmain() {
    myos::serial_init();
    myos::serial_write_line("MYOS: kernel entry");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        myos::serial_write_line("MYOS: unsupported Limine base revision");
        myos_halt_forever();
    }

    call_global_constructors();

    if (framebuffer_request.response == nullptr ||
        framebuffer_request.response->framebuffer_count == 0) {
        myos::serial_write_line("MYOS: no framebuffer from Limine");
        myos_halt_forever();
    }

    bool drew_screen = false;
    for (uint64_t i = 0; i < framebuffer_request.response->framebuffer_count; ++i) {
        limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[i];
        if (myos::draw_welcome(framebuffer)) {
            drew_screen = true;
        }
    }

    if (!drew_screen) {
        myos::serial_write_line("MYOS: no supported 32-bit RGB framebuffer");
        myos_halt_forever();
    }

    myos::serial_write_line("MYOS: boot ok v0.1.0");
    myos_halt_forever();
}

