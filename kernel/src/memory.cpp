#include <myos/memory.hpp>

namespace {
myos::FrameAllocator allocator;
constexpr uint64_t page = myos::FrameAllocator::PAGE_SIZE;
constexpr size_t max_entries = 4096;
bool bit(const uint8_t *map, size_t index) { return (map[index / 8] & (1u << (index % 8))) != 0; }
void set(uint8_t *map, size_t index, bool value) {
    const uint8_t mask = static_cast<uint8_t>(1u << (index % 8));
    if (value) { map[index / 8] |= mask; }
    else { map[index / 8] &= static_cast<uint8_t>(~mask); }
}
uint64_t first_frame(uint64_t address) {
    const uint64_t first = address / page + (address % page != 0);
    return first ? first : 1;
}
bool valid_map(limine_memmap_entry *const *entries, size_t count) {
    if (!entries || !count || count > max_entries) { return false; }
    for (size_t i = 0; i < count; ++i) {
        const auto *e = entries[i];
        if (!e || e->length > UINT64_MAX - e->base || (i && e->base < entries[i - 1]->base)) {
            return false;
        }
    }
    // Reserved entries may overlap each other, but never eligible RAM.
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
    return true;
}
}

namespace myos {
FrameAllocator &physical_frames() { return allocator; }

bool FrameAllocator::storage_plan(limine_memmap_entry *const *entries, size_t count, FrameStorage &storage) {
    storage = {};
    if (!valid_map(entries, count)) { return false; }
    uint64_t frames = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto &e = *entries[i];
        if (e.type != LIMINE_MEMMAP_USABLE) { continue; }
        const uint64_t first = first_frame(e.base), stop = (e.base + e.length) / page;
        if (stop > first) { frames += stop - first; }
    }
    const uint64_t bytes = (frames / 8 + (frames % 8 != 0) + page - 1) / page * page;
    if (!bytes || bytes > SIZE_MAX || frames <= bytes / page) { return false; }
    for (size_t i = 0; i < count; ++i) {
        const auto &e = *entries[i];
        if (e.type != LIMINE_MEMMAP_USABLE) { continue; }
        const uint64_t first = first_frame(e.base), stop = (e.base + e.length) / page;
        if (stop > first && stop - first >= bytes / page) {
            storage = {first * page, static_cast<size_t>(bytes)};
            return true;
        }
    }
    return false;
}

bool FrameAllocator::init(limine_memmap_entry *const *entries, size_t count, FrameStorage storage, void *mapping) {
    allocated_ = nullptr;
    region_count_ = cursor_ = 0;
    storage_ = {};
    detected_bytes_ = usable_bytes_ = managed_ = free_ = 0;
    FrameStorage expected;
    if (!mapping || !storage_plan(entries, count, expected) || storage.address != expected.address ||
        storage.bytes != expected.bytes) { return false; }
    allocated_ = static_cast<uint8_t *>(mapping);
    storage_ = storage;
    for (size_t i = 0; i < storage.bytes; ++i) { allocated_[i] = 0; }
    for (size_t i = 0; i < count; ++i) {
        const auto &e = *entries[i];
        if (e.type != LIMINE_MEMMAP_USABLE) { continue; }
        usable_bytes_ += e.length;
        const uint64_t first = first_frame(e.base), stop = (e.base + e.length) / page;
        if (stop <= first) { continue; }
        const size_t frames = static_cast<size_t>(stop - first);
        regions_[region_count_++] = {first * page, static_cast<size_t>(managed_), frames};
        if (storage.address >= first * page && storage.address < stop * page) {
            const size_t index = static_cast<size_t>(managed_ + storage.address / page - first);
            for (size_t n = 0; n < storage.bytes / page; ++n) { set(allocated_, index + n, true); }
        }
        managed_ += frames;
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
    free_ = managed_ - storage.bytes / page;
    return true;
}

uint64_t FrameAllocator::highest_address() const {
    if (!region_count_) { return 0; }
    const auto &region = regions_[region_count_ - 1];
    return region.address + region.count * page;
}

uint64_t FrameAllocator::allocate(uint64_t minimum_address) {
    if (!free_) { return 0; }
    const uint64_t minimum = first_frame(minimum_address);
    size_t begin = static_cast<size_t>(managed_);
    for (size_t r = 0; r < region_count_; ++r) {
        const auto &region = regions_[r];
        const uint64_t first = region.address / page;
        if (minimum >= first + region.count) { continue; }
        begin = region.first + static_cast<size_t>(minimum > first ? minimum - first : 0);
        break;
    }
    if (begin == managed_) { return 0; }
    size_t index = cursor_ >= begin ? cursor_ : begin;
    for (size_t checked = 0; checked < managed_ - begin; ++checked) {
        const size_t frame = index++;
        if (index == managed_) { index = begin; }
        if (bit(allocated_, frame)) { continue; }
        set(allocated_, frame, true);
        --free_;
        cursor_ = index;
        size_t low = 0, high = region_count_;
        while (low + 1 < high) {
            const size_t middle = low + (high - low) / 2;
            if (regions_[middle].first <= frame) { low = middle; }
            else { high = middle; }
        }
        return regions_[low].address + (frame - regions_[low].first) * page;
    }
    return 0;
}
bool FrameAllocator::release(uint64_t address) {
    if (!address || address % page || !region_count_ ||
        (address >= storage_.address && address - storage_.address < storage_.bytes)) { return false; }
    size_t low = 0, high = region_count_;
    while (low + 1 < high) {
        const size_t middle = low + (high - low) / 2;
        if (regions_[middle].address <= address) { low = middle; }
        else { high = middle; }
    }
    const auto &region = regions_[low];
    if (address < region.address || (address - region.address) / page >= region.count) { return false; }
    const size_t index = region.first + static_cast<size_t>((address - region.address) / page);
    if (!bit(allocated_, index)) { return false; }
    set(allocated_, index, false);
    ++free_;
    cursor_ = index;
    return true;
}
}
