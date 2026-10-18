#ifndef MYOS_SMBIOS_HPP
#define MYOS_SMBIOS_HPP
#include <stddef.h>
#include <stdint.h>
#include <limine.h>

namespace myos {
constexpr size_t SMBIOS_TABLE_LIMIT = 4 * 1024 * 1024;
struct SmbiosTable {
    uint64_t address = 0;
    size_t size = 0;
    uint16_t count = 0;
    bool exact_size = false;
};
bool smbios_entry(const uint8_t *bytes, size_t size, SmbiosTable &table);
uint64_t smbios_cpu_boot_mhz(const uint8_t *bytes, size_t size, uint16_t count = 0);
size_t smbios_mapped_size(uint64_t physical, size_t requested,
                         limine_memmap_entry *const *entries, size_t count);
uint64_t smbios_firmware_mhz(const limine_smbios_response *response,
                            const limine_memmap_response *map, uint64_t offset);
}
#endif
