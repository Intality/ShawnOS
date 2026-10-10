#include <stdint.h>

extern "C" void myos_halt_forever();

extern "C" {

void *__dso_handle = nullptr;

int __cxa_atexit(void (*)(void *), void *, void *) {
    return 0;
}

int __cxa_guard_acquire(uint64_t *guard) {
    return *reinterpret_cast<uint8_t *>(guard) == 0;
}

void __cxa_guard_release(uint64_t *guard) {
    *reinterpret_cast<uint8_t *>(guard) = 1;
}

void __cxa_guard_abort(uint64_t *) {
}

void __cxa_pure_virtual() {
    myos_halt_forever();
}

void __cxa_deleted_virtual() {
    myos_halt_forever();
}

}

