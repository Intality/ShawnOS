#include <myos/memory.hpp>

namespace {
myos::FrameAllocator allocator;
bool bit(const uint8_t *map, size_t index) { return (map[index / 8] & (1u << (index % 8))) != 0; }
void set(uint8_t *map, size_t index, bool value) {
    const uint8_t mask = static_cast<uint8_t>(1u << (index % 8));
    if (value) { map[index / 8] |= mask; }
    else { map[index / 8] &= static_cast<uint8_t>(~mask); }
}
}

namespace myos {
FrameAllocator &physical_frames() { return allocator; }
bool FrameAllocator::init(limine_memmap_entry *const *entries, size_t count) {
    detected_bytes_ = usable_bytes_ = managed_ = free_ = 0;
    cursor_ = end_ = 1;
    for (size_t i = 0; i < sizeof(eligible_); ++i) { eligible_[i] = allocated_[i] = 0; }
    if (!entries || !count || count > 4096) { return false; }
    for (size_t i = 0; i < count; ++i) {
        const auto *e = entries[i];
        if (!e || e->length > UINT64_MAX - e->base || (i && e->base < entries[i - 1]->base)) {
            return false;
        }
    }
    // Limine permits overlaps between reserved entries, but never with usable RAM.
    for (size_t i = 0; i < count; ++i) {
        const auto &e = *entries[i];
        if (e.type != LIMINE_MEMMAP_USABLE) { continue; }
        for (size_t j = 0; j < count; ++j) {
            if (i == j || !e.length || !entries[j]->length) { continue; }
            if (e.base < entries[j]->base + entries[j]->length && entries[j]->base < e.base + e.length) {
                return false;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const auto &e = *entries[i];
        if (e.type != LIMINE_MEMMAP_USABLE) { continue; }
        usable_bytes_ += e.length;
        if (e.base >= LIMIT) { continue; }
        const uint64_t last = e.base + e.length < LIMIT ? e.base + e.length : LIMIT;
        const size_t first = static_cast<size_t>((e.base + PAGE_SIZE - 1) / PAGE_SIZE);
        const size_t stop = static_cast<size_t>(last / PAGE_SIZE);
        for (size_t frame = first ? first : 1; frame < stop; ++frame) {
            set(eligible_, frame, true);
            ++managed_;
        }
        if (stop > end_) { end_ = stop; }
    }
    // Count the union of firmware-described RAM, excluding ambiguous reserved/MMIO ranges.
    uint64_t covered = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto &e = *entries[i];
        if (e.type != LIMINE_MEMMAP_USABLE && e.type != LIMINE_MEMMAP_ACPI_RECLAIMABLE &&
            e.type != LIMINE_MEMMAP_ACPI_NVS && e.type != LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE &&
            e.type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES) { continue; }
        const uint64_t stop = e.base + e.length;
        const uint64_t start = e.base > covered ? e.base : covered;
        if (stop > start) { detected_bytes_ += stop - start; }
        if (stop > covered) { covered = stop; }
    }
    free_ = managed_;
    return free_ != 0;
}
uint64_t FrameAllocator::allocate() {
    if (!free_) { return 0; }
    for (size_t checked = 0; checked < end_ - 1; ++checked) {
        const size_t frame = cursor_++;
        if (cursor_ >= end_) { cursor_ = 1; }
        if (bit(eligible_, frame) && !bit(allocated_, frame)) {
            set(allocated_, frame, true);
            --free_;
            return frame * PAGE_SIZE;
        }
    }
    return 0;
}
bool FrameAllocator::release(uint64_t address) {
    if (!address || address >= LIMIT || address % PAGE_SIZE) { return false; }
    const size_t frame = static_cast<size_t>(address / PAGE_SIZE);
    if (!bit(eligible_, frame) || !bit(allocated_, frame)) { return false; }
    set(allocated_, frame, false);
    ++free_;
    cursor_ = frame;
    return true;
}
}
