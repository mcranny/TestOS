#include "boot/limine_boot.h"
#include "platform.h"

/* Delimit requests and retain them in the loaded ELF image. */
__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = {
    0xf6b8f4b39de7d1aeULL, 0xfab91a6940fcb9cfULL,
    0x785c6ed015d3e316ULL, 0x181e920a7852b9d9ULL
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID
};
__attribute__((used, section(".limine_requests")))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST_ID
};
__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = {
    0xadc0e0531bb10d03ULL, 0x9572709f31764c62ULL
};

extern char __kernel_start[];
extern char __kernel_end[];

static int valid_framebuffer(struct limine_framebuffer *fb)
{
    uint64_t minimum_pitch, total;
    if (!fb || !fb->address || fb->memory_model != LIMINE_FRAMEBUFFER_RGB ||
        fb->width == 0 || fb->height == 0 || fb->bpp != 32 || fb->bpp % 8 != 0) {
        return 0;
    }
    if (fb->width > UINT64_MAX / 4 || fb->pitch < fb->width * 4) return 0;
    if (fb->height > UINT64_MAX / fb->pitch) return 0;
    minimum_pitch = fb->width * 4;
    total = fb->pitch * fb->height;
    if (minimum_pitch == 0 || total == 0 || fb->red_mask_size == 0 ||
        fb->green_mask_size == 0 || fb->blue_mask_size == 0 ||
        fb->red_mask_size + fb->red_mask_shift > 32 ||
        fb->green_mask_size + fb->green_mask_shift > 32 ||
        fb->blue_mask_size + fb->blue_mask_shift > 32) {
        return 0;
    }
    return 1;
}

static int framebuffer_is_reserved(struct limine_framebuffer *fb, uint64_t hhdm_offset,
                                   struct limine_memmap_response *map)
{
    uint64_t physical, size, end, index;
    if (map->entry_count == 0 || map->entry_count > 512 || !map->entries ||
        (uint64_t)(uintptr_t)fb->address < hhdm_offset ||
        fb->height > UINT64_MAX / fb->pitch) {
        return 0;
    }
    physical = (uint64_t)(uintptr_t)fb->address - hhdm_offset;
    size = fb->pitch * fb->height;
    if (physical > UINT64_MAX - size) return 0;
    end = physical + size;
    for (index = 0; index < map->entry_count; index++) {
        struct limine_memmap_entry *entry = map->entries[index];
        uint64_t entry_end;
        if (!entry || entry->length == 0 || entry->base > UINT64_MAX - entry->length) return 0;
        entry_end = entry->base + entry->length;
        if (entry->type == LIMINE_MEMMAP_FRAMEBUFFER && physical >= entry->base && end <= entry_end) {
            return 1;
        }
    }
    return 0;
}

int boot_init(struct boot_info *out)
{
    struct limine_framebuffer *fb;

    if (!out) return -1;
    if (!hhdm_request.response) {
        panic("E001A HHDM MISSING");
    }
    if (!memmap_request.response) {
        panic("E001B MEMORY MAP MISSING");
    }
    if (!framebuffer_request.response) {
        panic("E001C FRAMEBUFFER MISSING");
    }
    if (!kernel_address_request.response) {
        panic("E001F KERNEL ADDRESS MISSING");
    }
    if (hhdm_request.response->revision > 1 || memmap_request.response->revision > 1 ||
        framebuffer_request.response->revision > 2 ||
        kernel_address_request.response->revision > 0) {
        panic("E001D RESPONSE REVISION");
    }
    if (framebuffer_request.response->framebuffer_count == 0 ||
        framebuffer_request.response->framebuffer_count > 16 ||
        !framebuffer_request.response->framebuffers) {
        panic("E001E FRAMEBUFFER RESPONSE");
    }

    fb = framebuffer_request.response->framebuffers[0];
    if (!valid_framebuffer(fb)) {
        panic("E002 FRAMEBUFFER INVALID");
    }
    if (!framebuffer_is_reserved(fb, hhdm_request.response->offset, memmap_request.response)) {
        panic("E003 MEMORY MAP INVALID");
    }

    out->hhdm_offset = hhdm_request.response->offset;
    out->kernel_phys_base = kernel_address_request.response->physical_base;
    out->kernel_virt_base = kernel_address_request.response->virtual_base;
    out->kernel_size = (uint64_t)(__kernel_end - __kernel_start);
    out->memmap = memmap_request.response;
    out->framebuffer = fb;
    return 0;
}
