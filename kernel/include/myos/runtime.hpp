#ifndef MYOS_RUNTIME_HPP
#define MYOS_RUNTIME_HPP
#include <stdint.h>
#include <myos/cpu.hpp>

namespace myos {
class Canvas;
struct InterruptFrame {
    uint64_t registers[15];
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};
constexpr uint64_t TIMER_HZ = 100;
void interrupts_init();
void timer_start(uint64_t direct_map_offset);
void clock_init(uint64_t firmware_mhz = 0);
CpuFrequency cpu_frequency();
uint64_t tsc_mhz();
uint64_t timer_ticks();
uint64_t idle_wakes();
void idle_wait();
void panic_attach(Canvas *canvas);
[[noreturn]] void panic(const char *reason, const InterruptFrame *frame = nullptr, uint64_t fault_address = 0);
void exception_selftest(const char *command_line);
}
#endif
