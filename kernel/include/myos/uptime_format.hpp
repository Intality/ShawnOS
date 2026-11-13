#ifndef MYOS_UPTIME_FORMAT_HPP
#define MYOS_UPTIME_FORMAT_HPP

#include <stdint.h>
#include <myos/text_buffer.hpp>

namespace myos {

inline TextBuffer<32> uptime_text(uint64_t seconds) {
    TextBuffer<32> text;
    const uint64_t values[] = {seconds / 86400, seconds / 3600 % 24,
                               seconds / 60 % 60, seconds % 60};
    constexpr char units[] = "dhms";
    for (unsigned i = 0; i < 4; ++i) {
        if (!text.size && !values[i] && i < 3) { continue; }
        if (text.size) { text.append(' '); }
        uint64_t value = values[i];
        char digits[20];
        unsigned count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);
        while (count) { text.append(digits[--count]); }
        text.append(units[i]);
    }
    return text;
}

} // namespace myos

#endif
