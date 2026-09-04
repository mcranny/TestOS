#ifndef TESTOS_UEFI_LIMINE_H
#define TESTOS_UEFI_LIMINE_H

/*
 * Minimal, pinned subset of Limine's public v12 protocol header.
 * Source: https://github.com/limine-bootloader/limine-protocol
 * All protocol pointers and integer fields are deliberately 64-bit.
 */
#include "types.h"

#define LIMINE_COMMON_MAGIC 0xc7b1dd30df4c8b88ULL, 0x0a82e883a194f07bULL
#define LIMINE_FRAMEBUFFER_REQUEST_ID { LIMINE_COMMON_MAGIC, 0x9d5827dcd881dd75ULL, 0xa3148604f6fab11bULL }
#define LIMINE_HHDM_REQUEST_ID        { LIMINE_COMMON_MAGIC, 0x48dcf1cb8ad2b852ULL, 0x63984e959a98244bULL }
#define LIMINE_MEMMAP_REQUEST_ID      { LIMINE_COMMON_MAGIC, 0x67cf3d9d378a806fULL, 0xe304acdfc50c3c62ULL }

#define LIMINE_FRAMEBUFFER_RGB 1ULL
#define LIMINE_MEMMAP_FRAMEBUFFER 7ULL

struct limine_framebuffer {
    void *address;
    uint64_t width, height, pitch;
    uint16_t bpp;
    uint8_t memory_model;
    uint8_t red_mask_size, red_mask_shift;
    uint8_t green_mask_size, green_mask_shift;
    uint8_t blue_mask_size, blue_mask_shift;
    uint8_t unused[7];
    uint64_t edid_size;
    void *edid;
};

struct limine_framebuffer_response {
    uint64_t revision, framebuffer_count;
    struct limine_framebuffer **framebuffers;
};
struct limine_framebuffer_request {
    uint64_t id[4], revision;
    struct limine_framebuffer_response *response;
};
struct limine_hhdm_response { uint64_t revision, offset; };
struct limine_hhdm_request {
    uint64_t id[4], revision;
    struct limine_hhdm_response *response;
};
struct limine_memmap_entry { uint64_t base, length, type; };
struct limine_memmap_response {
    uint64_t revision, entry_count;
    struct limine_memmap_entry **entries;
};
struct limine_memmap_request {
    uint64_t id[4], revision;
    struct limine_memmap_response *response;
};

#endif
