#ifndef MYOS_MEMORY_HPP
#define MYOS_MEMORY_HPP
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

namespace myos {
struct FrameStorage {
    uint64_t address = 0;
    size_t bytes = 0;
};

class FrameAllocator {
public:
    static constexpr uint64_t PAGE_SIZE = 4096;
    static bool storage_plan(limine_memmap_entry *const *entries, size_t count, FrameStorage &storage);
    bool init(limine_memmap_entry *const *entries, size_t count, FrameStorage storage, void *mapping);
    uint64_t allocate(uint64_t minimum_address = PAGE_SIZE); // Physical address; zero means exhausted.
    bool release(uint64_t address);
    uint64_t detected_bytes() const { return detected_bytes_; }
    uint64_t usable_bytes() const { return usable_bytes_; }
    uint64_t managed_frames() const { return managed_; }
    uint64_t free_frames() const { return free_; }
    FrameStorage storage() const { return storage_; }
    uint64_t highest_address() const;

private:
    struct Region { uint64_t address; size_t first, count; };
    static constexpr size_t MAX_REGIONS = 4096;
    Region regions_[MAX_REGIONS] = {};
    size_t region_count_ = 0;
    uint8_t *allocated_ = nullptr;
    FrameStorage storage_;
    uint64_t detected_bytes_ = 0;
    uint64_t usable_bytes_ = 0, managed_ = 0, free_ = 0;
    size_t cursor_ = 0;
};
FrameAllocator &physical_frames();
}
#endif
