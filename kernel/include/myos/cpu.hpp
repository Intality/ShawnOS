#ifndef MYOS_CPU_HPP
#define MYOS_CPU_HPP

#include <stddef.h>
#include <stdint.h>
#include <myos/text_buffer.hpp>

namespace myos {

struct CpuIdRegisters {
    uint32_t eax, ebx, ecx, edx;
};

enum class CpuFrequencySource { Unavailable, CpuidBase, IntelBrand, FirmwareBoot };
struct CpuFrequency {
    uint64_t mhz = 0;
    CpuFrequencySource source = CpuFrequencySource::Unavailable;
    constexpr const char *label() const {
        return source == CpuFrequencySource::FirmwareBoot ? "CPU boot speed" : "CPU base speed";
    }
};
constexpr CpuFrequency select_cpu_frequency(uint64_t cpuid_mhz, uint64_t brand_mhz,
                                            uint64_t firmware_mhz) {
    if (cpuid_mhz) return {cpuid_mhz, CpuFrequencySource::CpuidBase};
    if (brand_mhz) return {brand_mhz, CpuFrequencySource::IntelBrand};
    if (firmware_mhz) return {firmware_mhz, CpuFrequencySource::FirmwareBoot};
    return {};
}

constexpr bool is_intel_cpu(const CpuIdRegisters &identity) {
    return identity.ebx == 0x756e6547 &&
           identity.edx == 0x49656e69 &&
           identity.ecx == 0x6c65746e;
}

constexpr bool has_cpu_base_frequency(const CpuIdRegisters &identity) {
    // Intel defines leaf 0x16 as a reported base frequency, not live speed.
    return identity.eax >= 0x16 && is_intel_cpu(identity);
}

constexpr uint64_t reported_cpu_base_mhz(const CpuIdRegisters &identity,
                                       uint32_t frequency_eax) {
    return has_cpu_base_frequency(identity) ? frequency_eax & 0xffffu : 0;
}

constexpr size_t CPU_BRAND_BYTES = 48;

inline uint64_t cpu_brand_base_mhz(const CpuIdRegisters &identity,
                                  const char *brand, size_t size) {
    if (!is_intel_cpu(identity) || !brand) return 0;
    size_t end = 0;
    while (end < size && end < CPU_BRAND_BYTES && brand[end]) ++end;
    while (end && brand[end - 1] == ' ') --end;
    size_t at = end;
    for (size_t i = 0; i < end; ++i) {
        if (brand[i] == '@') {
            if (at != end) return 0;
            at = i;
        }
    }
    if (at == end) return 0;
    ++at;
    while (at < end && brand[at] == ' ') ++at;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const size_t first = at;
    uint64_t whole = 0;
    while (at < end && digit(brand[at])) {
        const uint64_t value = static_cast<unsigned>(brand[at++] - '0');
        if (whole > (UINT64_MAX - value) / 10) return 0;
        whole = whole * 10 + value;
    }
    if (at == first) return 0;
    uint64_t fraction = 0, scale = 1;
    if (at < end && brand[at] == '.') {
        const size_t fraction_start = ++at;
        while (at < end && digit(brand[at])) {
            if (scale == 1000) return 0;
            fraction = fraction * 10 + static_cast<unsigned>(brand[at++] - '0');
            scale *= 10;
        }
        if (at == fraction_start) return 0;
    }
    while (at < end && brand[at] == ' ') ++at;
    if (end - at != 3 || (brand[at] != 'G' && brand[at] != 'M') ||
        brand[at + 1] != 'H' || brand[at + 2] != 'z') return 0;
    // Only an explicit Intel brand suffix is used as a nominal base specification.
    const uint64_t multiplier = brand[at] == 'G' ? 1000 : 1;
    if ((multiplier == 1 && fraction) || whole > UINT64_MAX / multiplier) return 0;
    const uint64_t base = whole * multiplier;
    const uint64_t remainder = fraction * multiplier / scale;
    if (base > UINT64_MAX - remainder) return 0;
    return base + remainder;
}

inline TextBuffer<40> cpu_frequency_text(uint64_t mhz) {
    TextBuffer<40> result;
    const auto append_number = [&result](uint64_t value) {
        char digits[20];
        unsigned count = 0;
        do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);
        while (count) result.append(digits[--count]);
    };
    if (!mhz) {
        for (const char c : "Unavailable") result.append(c);
    } else if (mhz < 1000) {
        append_number(mhz);
        for (const char c : " MHz") result.append(c);
    } else {
        const uint64_t hundredths = mhz / 10 + (mhz % 10 >= 5);
        append_number(hundredths / 100);
        result.append('.');
        result.append(static_cast<char>('0' + hundredths % 100 / 10));
        result.append(static_cast<char>('0' + hundredths % 10));
        for (const char c : " GHz") result.append(c);
    }
    return result;
}

} // namespace myos

#endif
