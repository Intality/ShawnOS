#ifndef MYOS_MEMORY_FORMAT_HPP
#define MYOS_MEMORY_FORMAT_HPP

#include <stdint.h>
#include <myos/text_buffer.hpp>

namespace myos {

inline TextBuffer<40> memory_size_text(uint64_t bytes) {
    TextBuffer<40> result;
    // Use the familiar 1024-based RAM convention with user-facing MB/GB labels.
    const bool gigabytes = bytes >= (1ull << 30);
    const uint64_t unit = gigabytes ? (1ull << 30) : (1ull << 20);
    // Only multiply the bounded remainder; handle a rounding carry separately.
    const uint64_t hundredths = ((bytes % unit) * 100 + unit / 2) / unit;
    uint64_t whole = bytes / unit + hundredths / 100;
    char digits[20];
    unsigned count = 0;
    do {
        digits[count++] = static_cast<char>('0' + whole % 10);
        whole /= 10;
    } while (whole);
    while (count) result.append(digits[--count]);
    result.append('.');
    result.append(static_cast<char>('0' + hundredths % 100 / 10));
    result.append(static_cast<char>('0' + hundredths % 10));
    for (const char *suffix = gigabytes ? " GB" : " MB"; *suffix; ++suffix) result.append(*suffix);
    return result;
}

} // namespace myos

#endif
