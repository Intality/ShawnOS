#ifndef MYOS_TEXT_BUFFER_HPP
#define MYOS_TEXT_BUFFER_HPP
#include <stddef.h>
namespace myos {
template <size_t Capacity> struct TextBuffer {
    static_assert(Capacity > 1);
    char data[Capacity] = {};
    size_t size = 0;
    bool append(char c) {
        if (c == 0 || size == Capacity - 1) { return false; }
        data[size++] = c;
        data[size] = 0;
        return true;
    }
    bool backspace() {
        if (size == 0) { return false; }
        data[--size] = 0;
        return true;
    }
    void clear() { size = 0; data[0] = 0; }
};
}
#endif
