#ifndef MYOS_MEMORY_HPP
#define MYOS_MEMORY_HPP
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

namespace myos {
class FrameAllocator {
public:
    static constexpr uint64_t PAGE_SIZE = 4096;
    static constexpr uint64_t LIMIT = 1ull << 32;
    bool init(limine_memmap_entry *const *entries, size_t count);
    uint64_t allocate(); // Physical address; zero means exhausted.
    bool release(uint64_t address);
    uint64_t detected_bytes() const { return detected_bytes_; }
    uint64_t usable_bytes() const { return usable_bytes_; }
    uint64_t managed_frames() const { return managed_; }
    uint64_t free_frames() const { return free_; }

private:
    static constexpr size_t FRAME_COUNT = LIMIT / PAGE_SIZE;
    uint8_t eligible_[FRAME_COUNT / 8] = {};
    uint8_t allocated_[FRAME_COUNT / 8] = {};
    uint64_t detected_bytes_ = 0;
    uint64_t usable_bytes_ = 0, managed_ = 0, free_ = 0;
    size_t cursor_ = 1, end_ = 1;
};
FrameAllocator &physical_frames();
}
#endif
