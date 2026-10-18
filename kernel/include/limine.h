/* SPDX-License-Identifier: 0BSD */
/* Minimal Limine protocol declarations used by ShawnOS v0.3.0. */
#ifndef MYOS_LIMINE_H
#define MYOS_LIMINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef LIMINE_NO_POINTERS
#define LIMINE_PTR(TYPE) uint64_t
#else
#define LIMINE_PTR(TYPE) TYPE
#endif

#define LIMINE_REQUESTS_START_MARKER { 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, \
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9 }
#define LIMINE_REQUESTS_END_MARKER { 0xadc0e0531bb10d03, 0x9572709f31764c62 }
#define LIMINE_BASE_REVISION(N) { 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, (N) }
#define LIMINE_BASE_REVISION_SUPPORTED(VAR) ((VAR)[2] == 0)
#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88, 0x0a82e883a194f07b

#define LIMINE_FRAMEBUFFER_REQUEST_ID { LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75, 0xa3148604f6fab11b }
#define LIMINE_FRAMEBUFFER_RGB 1

struct limine_video_mode {
    uint64_t pitch;
    uint64_t width;
    uint64_t height;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

struct limine_framebuffer {
    LIMINE_PTR(void *) address;
    uint64_t width;
    uint64_t height;
    uint64_t pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size;
    uint8_t red_mask_shift;
    uint8_t green_mask_size;
    uint8_t green_mask_shift;
    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    LIMINE_PTR(void *) edid;
    uint64_t mode_count;
    LIMINE_PTR(struct limine_video_mode **) modes;
};

struct limine_framebuffer_response {
    uint64_t revision;
    uint64_t framebuffer_count;
    LIMINE_PTR(struct limine_framebuffer **) framebuffers;
};

struct limine_framebuffer_request {
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_framebuffer_response *) response;
};

#define LIMINE_HHDM_REQUEST_ID { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }
struct limine_hhdm_response { uint64_t revision; uint64_t offset; };
struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_hhdm_response *) response;
};

#define LIMINE_MEMMAP_REQUEST_ID { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }
#define LIMINE_MEMMAP_USABLE 0
#define LIMINE_MEMMAP_RESERVED 1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE 2
#define LIMINE_MEMMAP_ACPI_NVS 3
#define LIMINE_MEMMAP_BAD_MEMORY 4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER 7
#define LIMINE_MEMMAP_RESERVED_MAPPED 8
struct limine_memmap_entry { uint64_t base; uint64_t length; uint64_t type; };
struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    LIMINE_PTR(struct limine_memmap_entry **) entries;
};
struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_memmap_response *) response;
};

#define LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID { LIMINE_COMMON_MAGIC, 0x4b161536e598651e, 0xb390ad4a2f1f303a }
struct limine_executable_cmdline_response { uint64_t revision; LIMINE_PTR(char *) cmdline; };
struct limine_executable_cmdline_request {
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_executable_cmdline_response *) response;
};

#define LIMINE_SMBIOS_REQUEST_ID { LIMINE_COMMON_MAGIC, 0x9e9046f11e095391, 0xaa4a520fefbde5ee }
struct limine_smbios_response {
    uint64_t revision;
    LIMINE_PTR(void *) entry_32;
    LIMINE_PTR(void *) entry_64;
};
struct limine_smbios_request {
    uint64_t id[4];
    uint64_t revision;
    LIMINE_PTR(struct limine_smbios_response *) response;
};

#ifdef __cplusplus
}
#endif

#endif

