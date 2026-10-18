#include <myos/smbios.hpp>

namespace {
uint64_t little(const uint8_t *p, unsigned count) {
    uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= static_cast<uint64_t>(p[i]) << (i * 8);
    return value;
}
bool signature(const uint8_t *p, const char *text, size_t size) {
    for (size_t i = 0; i < size; ++i) if (p[i] != static_cast<uint8_t>(text[i])) return false;
    return true;
}
bool checksum(const uint8_t *p, size_t size) {
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i) sum += p[i];
    return sum == 0;
}
}

namespace myos {
bool smbios_entry(const uint8_t *p, size_t size, SmbiosTable &table) {
    table = {};
    if (!p || size < 7) return false;
    SmbiosTable candidate;
    if (signature(p, "_SM3_", 5)) {
        const size_t length = p[6];
        if (length < 24 || length > size || p[7] < 3 || p[10] != 1 ||
            !checksum(p, length)) return false;
        candidate.address = little(p + 16, 8);
        candidate.size = little(p + 12, 4);
    } else if (signature(p, "_SM_", 4)) {
        const size_t length = p[5];
        // SMBIOS 2.1 permits the historical 0x1e length, but DMI still needs 31 bytes.
        if (size < 31 || length < 30 || length > size || p[6] < 2 ||
            (length == 30 && (p[6] != 2 || p[7] != 1)) ||
            !signature(p + 16, "_DMI_", 5) || !checksum(p, length) ||
            !checksum(p + 16, 15)) return false;
        candidate.address = little(p + 24, 4);
        candidate.size = little(p + 22, 2);
        candidate.count = little(p + 28, 2);
        candidate.exact_size = true;
        if (!candidate.count) return false;
    } else return false;
    if (!candidate.address || !candidate.size || candidate.size > SMBIOS_TABLE_LIMIT ||
        candidate.address > UINT64_MAX - candidate.size) return false;
    table = candidate;
    return true;
}

uint64_t smbios_cpu_boot_mhz(const uint8_t *p, size_t size, uint16_t count) {
    if (!p || size > SMBIOS_TABLE_LIMIT) return 0;
    size_t offset = 0, seen = 0;
    uint64_t speed = 0;
    bool ambiguous = false;
    while (offset < size && (!count || seen < count)) {
        if (size - offset < 4) return 0;
        const uint8_t type = p[offset], length = p[offset + 1];
        if (length < 4 || length > size - offset) return 0;
        size_t next = offset + length;
        while (next < size && size - next >= 2 && (p[next] || p[next + 1])) ++next;
        if (size - next < 2) return 0;
        next += 2;
        ++seen;
        if (type == 4) {
            if (length < 0x1a) return 0;
            const uint8_t status = p[offset + 0x18];
            if ((status & 0x40) && (status & 7) == 1 && p[offset + 5] == 3) {
                const uint64_t reported = little(p + offset + 0x16, 2);
                if (!reported || reported == 0xffff || (speed && speed != reported)) ambiguous = true;
                if (reported) speed = reported;
            }
        }
        if (type == 127) return (!count || seen == count) && !ambiguous ? speed : 0;
        offset = next;
    }
    return count && seen == count && offset == size && !ambiguous ? speed : 0;
}

size_t smbios_mapped_size(uint64_t physical, size_t requested,
                         limine_memmap_entry *const *entries, size_t count) {
    if (!physical || !requested || requested > SMBIOS_TABLE_LIMIT || !entries ||
        count > 4096 || physical > UINT64_MAX - requested) return 0;
    uint64_t end = physical;
    const uint64_t limit = physical + requested;
    for (size_t i = 0; i < count; ++i) {
        const auto *region = entries[i];
        if (!region || region->base > UINT64_MAX - region->length) return 0;
        if (region->type != LIMINE_MEMMAP_RESERVED_MAPPED) continue;
        const uint64_t region_end = region->base + region->length;
        if (region->base <= end && region_end > end) {
            end = region_end < limit ? region_end : limit;
            if (end == limit) break;
        }
    }
    return static_cast<size_t>(end - physical);
}

uint64_t smbios_firmware_mhz(const limine_smbios_response *response,
                            const limine_memmap_response *map, uint64_t offset) {
    if (!response || !map) return 0;
    // Base revision 6 maps SMBIOS entry points and physical tables as reserved-mapped.
    const void *candidates[] = {response->entry_64, response->entry_32};
    for (const void *candidate : candidates) {
        const uint64_t virtual_address = reinterpret_cast<uint64_t>(candidate);
        if (!candidate || virtual_address < offset) continue;
        const size_t entry_size = smbios_mapped_size(virtual_address - offset, 255,
                                                   map->entries, map->entry_count);
        if (virtual_address > UINT64_MAX - entry_size) continue;
        SmbiosTable table;
        if (!smbios_entry(static_cast<const uint8_t *>(candidate), entry_size, table)) continue;
        const size_t available = smbios_mapped_size(table.address, table.size, map->entries, map->entry_count);
        if (!available || (table.exact_size && available != table.size) ||
            offset > UINT64_MAX - table.address || available > UINT64_MAX - (offset + table.address)) continue;
        const auto *bytes = reinterpret_cast<const uint8_t *>(offset + table.address);
        if (const uint64_t mhz = smbios_cpu_boot_mhz(bytes, available, table.count)) return mhz;
    }
    return 0;
}
}
