#ifndef MYOS_DESKTOP_HPP
#define MYOS_DESKTOP_HPP
#include <limine.h>
namespace myos {
bool desktop_init(limine_framebuffer *framebuffer);
[[noreturn]] void desktop_run();
}
#endif
