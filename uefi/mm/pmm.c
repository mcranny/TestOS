#include "mm/pmm.h"
#include "platform.h"

#define PAGE_SIZE 4096ULL
/* Bitmap covering the first 4 GiB of physical address space. */
#define PMM_MAX_PHYS (4ULL * 1024ULL * 1024ULL * 1024ULL)
#define PMM_BITMAP_BYTES (PMM_MAX_PHYS / PAGE_SIZE / 8ULL)

static uint8_t pmm_bitmap[PMM_BITMAP_BYTES];
static uint64_t total_frames;
static uint64_t free_count;
static uint64_t hhdm_offset;
static uint64_t kernel_phys_base;
static uint64_t kernel_virt_base;
static uint64_t kernel_size;
static uint64_t max_frame;

static void pmm_set(uint64_t frame, int used)
{
    uint64_t byte = frame / 8;
    uint8_t bit = (uint8_t)(frame % 8);
    if (byte >= PMM_BITMAP_BYTES) return;
    if (used) {
        if ((pmm_bitmap[byte] & (1U << bit)) == 0) {
            pmm_bitmap[byte] |= (uint8_t)(1U << bit);
            if (free_count > 0) free_count--;
        }
    } else {
        if ((pmm_bitmap[byte] & (1U << bit)) != 0) {
            pmm_bitmap[byte] &= (uint8_t)~(1U << bit);
            free_count++;
        }
    }
}

static int pmm_test(uint64_t frame)
{
    uint64_t byte = frame / 8;
    uint8_t bit = (uint8_t)(frame % 8);
    if (byte >= PMM_BITMAP_BYTES) return 1;
    return (pmm_bitmap[byte] & (1U << bit)) != 0;
}

static void mark_region(uint64_t base, uint64_t length, int used)
{
    uint64_t start = base / PAGE_SIZE;
    uint64_t end = (base + length + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t frame;
    if (length == 0) return;
    if (end > max_frame) end = max_frame;
    for (frame = start; frame < end; frame++) {
        pmm_set(frame, used);
    }
}

void pmm_init(const struct boot_info *boot)
{
    uint64_t i;
    struct limine_memmap_response *map = boot->memmap;

    hhdm_offset = boot->hhdm_offset;
    kernel_phys_base = boot->kernel_phys_base;
    kernel_virt_base = boot->kernel_virt_base;
    kernel_size = boot->kernel_size;
    max_frame = PMM_MAX_PHYS / PAGE_SIZE;
    total_frames = 0;
    free_count = 0;

    for (i = 0; i < PMM_BITMAP_BYTES; i++) {
        pmm_bitmap[i] = 0xff; /* everything used until freed */
    }

    for (i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];
        uint64_t base, length, end;
        if (!entry || entry->length == 0) continue;
        base = entry->base;
        length = entry->length;
        end = base + length;
        if (end > PMM_MAX_PHYS) {
            if (base >= PMM_MAX_PHYS) continue;
            length = PMM_MAX_PHYS - base;
        }
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            mark_region(base, length, 0);
            total_frames += length / PAGE_SIZE;
        }
    }

    /* Never hand out the null page. */
    pmm_set(0, 1);

    /* Reserve kernel image frames. */
    mark_region(boot->kernel_phys_base, boot->kernel_size, 1);

    /* Reserve framebuffer. */
    {
        struct limine_framebuffer *fb = boot->framebuffer;
        uint64_t fb_phys = (uint64_t)(uintptr_t)fb->address - boot->hhdm_offset;
        uint64_t fb_size = fb->pitch * fb->height;
        mark_region(fb_phys, fb_size, 1);
    }

    /* Bitmap itself lives in the kernel BSS, already reserved via kernel image. */
}

uint64_t pmm_alloc_frame(void)
{
    uint64_t frame;
    for (frame = 1; frame < max_frame; frame++) {
        if (!pmm_test(frame)) {
            pmm_set(frame, 1);
            return frame * PAGE_SIZE;
        }
    }
    return 0;
}

uint64_t pmm_alloc_contiguous(uint64_t frames)
{
    uint64_t start;
    uint64_t i;

    if (frames == 0) {
        return 0;
    }

    for (start = 1; start + frames <= max_frame; start++) {
        for (i = 0; i < frames; i++) {
            if (pmm_test(start + i)) {
                break;
            }
        }
        if (i == frames) {
            for (i = 0; i < frames; i++) {
                pmm_set(start + i, 1);
            }
            return start * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_frame(uint64_t phys)
{
    if (phys == 0 || (phys % PAGE_SIZE) != 0) return;
    pmm_set(phys / PAGE_SIZE, 0);
}

uint64_t pmm_total_frames(void)
{
    return total_frames;
}

uint64_t pmm_free_frames(void)
{
    return free_count;
}

void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(phys + hhdm_offset);
}

uint64_t virt_to_phys(const void *virt)
{
    uint64_t addr = (uint64_t)(uintptr_t)virt;
    if (kernel_size != 0 && addr >= kernel_virt_base &&
        addr < kernel_virt_base + kernel_size) {
        return kernel_phys_base + (addr - kernel_virt_base);
    }
    if (addr >= hhdm_offset) {
        return addr - hhdm_offset;
    }
    panic("virt_to_phys: address outside HHDM/kernel");
}
