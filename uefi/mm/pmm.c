#include "mm/pmm.h"
#include "sync/spinlock.h"
#include "platform.h"

#define PAGE_SIZE 4096ULL

static uint8_t *pmm_bitmap;
static uint64_t pmm_bitmap_bytes;
static uint64_t bitmap_phys_base;
static uint64_t bitmap_byte_len;
static uint64_t total_frames;
static uint64_t free_count;
static uint64_t hhdm_offset;
static uint64_t kernel_phys_base;
static uint64_t kernel_virt_base;
static uint64_t kernel_size;
static uint64_t max_frame;
static spinlock_t pmm_lock = SPINLOCK_INIT;

static void pmm_set(uint64_t frame, int used)
{
    uint64_t byte = frame / 8;
    uint8_t bit = (uint8_t)(frame % 8);
    if (pmm_bitmap == NULL || byte >= pmm_bitmap_bytes) return;
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
    if (pmm_bitmap == NULL || byte >= pmm_bitmap_bytes) return 1;
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

static uint64_t align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1ULL) & ~(align - 1ULL);
}

/* Pick a usable page-aligned range large enough for the frame bitmap. */
static int find_bitmap_storage(struct limine_memmap_response *map, uint64_t bytes_needed,
                               uint64_t *out_phys)
{
    uint64_t i;
    uint64_t needed = align_up(bytes_needed, PAGE_SIZE);

    for (i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];
        uint64_t base;
        uint64_t end;
        uint64_t available;

        if (!entry || entry->type != LIMINE_MEMMAP_USABLE || entry->length == 0) {
            continue;
        }

        base = align_up(entry->base, PAGE_SIZE);
        end = entry->base + entry->length;
        if (base >= end) {
            continue;
        }
        available = end - base;
        if (available < needed) {
            continue;
        }

        *out_phys = base;
        return 1;
    }
    return 0;
}

void pmm_init(const struct boot_info *boot)
{
    uint64_t i;
    uint64_t top = 0;
    struct limine_memmap_response *map = boot->memmap;

    hhdm_offset = boot->hhdm_offset;
    kernel_phys_base = boot->kernel_phys_base;
    kernel_virt_base = boot->kernel_virt_base;
    kernel_size = boot->kernel_size;
    total_frames = 0;
    free_count = 0;
    pmm_bitmap = NULL;
    pmm_bitmap_bytes = 0;
    bitmap_phys_base = 0;
    bitmap_byte_len = 0;
    max_frame = 0;

    /* Size the bitmap from the highest usable physical address. */
    for (i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];
        uint64_t end;
        if (!entry || entry->type != LIMINE_MEMMAP_USABLE || entry->length == 0) {
            continue;
        }
        end = entry->base + entry->length;
        if (end > top) {
            top = end;
        }
    }

    if (top < PAGE_SIZE) {
        panic("pmm_init: no usable memory");
    }

    max_frame = top / PAGE_SIZE;
    bitmap_byte_len = (max_frame + 7ULL) / 8ULL;
    if (!find_bitmap_storage(map, bitmap_byte_len, &bitmap_phys_base)) {
        panic("pmm_init: no room for frame bitmap");
    }

    pmm_bitmap_bytes = align_up(bitmap_byte_len, PAGE_SIZE);
    pmm_bitmap = (uint8_t *)(uintptr_t)(bitmap_phys_base + hhdm_offset);
    for (i = 0; i < pmm_bitmap_bytes; i++) {
        pmm_bitmap[i] = 0xff; /* everything used until freed */
    }

    for (i = 0; i < map->entry_count; i++) {
        struct limine_memmap_entry *entry = map->entries[i];
        if (!entry || entry->length == 0) continue;
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            mark_region(entry->base, entry->length, 0);
            total_frames += entry->length / PAGE_SIZE;
        }
    }

    /* Never hand out the null page. */
    pmm_set(0, 1);

    /* Reserve the dynamically placed bitmap. */
    mark_region(bitmap_phys_base, pmm_bitmap_bytes, 1);

    /* Reserve kernel image frames. */
    mark_region(boot->kernel_phys_base, boot->kernel_size, 1);

    /* Reserve framebuffer. */
    {
        struct limine_framebuffer *fb = boot->framebuffer;
        uint64_t fb_phys = (uint64_t)(uintptr_t)fb->address - boot->hhdm_offset;
        uint64_t fb_size = fb->pitch * fb->height;
        mark_region(fb_phys, fb_size, 1);
    }
}

uint64_t pmm_alloc_frame(void)
{
    return pmm_alloc_frame_below(~0ULL);
}

uint64_t pmm_alloc_frame_below(uint64_t max_phys)
{
    uint64_t frame;
    uint64_t limit = max_frame;
    uint64_t flags;
    uint64_t result = 0;

    flags = spin_lock_irqsave(&pmm_lock);
    if (max_phys / PAGE_SIZE < limit) {
        limit = max_phys / PAGE_SIZE;
    }

    for (frame = 1; frame < limit; frame++) {
        if (!pmm_test(frame)) {
            pmm_set(frame, 1);
            result = frame * PAGE_SIZE;
            break;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
    return result;
}

uint64_t pmm_alloc_contiguous(uint64_t frames)
{
    return pmm_alloc_contiguous_below(frames, ~0ULL);
}

uint64_t pmm_alloc_contiguous_below(uint64_t frames, uint64_t max_phys)
{
    uint64_t start;
    uint64_t i;
    uint64_t limit = max_frame;
    uint64_t flags;
    uint64_t result = 0;

    if (frames == 0) {
        return 0;
    }

    flags = spin_lock_irqsave(&pmm_lock);
    if (max_phys / PAGE_SIZE < limit) {
        limit = max_phys / PAGE_SIZE;
    }

    if (frames <= limit) {
        for (start = 1; start + frames <= limit; ) {
            for (i = 0; i < frames; i++) {
                if (pmm_test(start + i)) {
                    break;
                }
            }
            if (i == frames) {
                for (i = 0; i < frames; i++) {
                    pmm_set(start + i, 1);
                }
                result = start * PAGE_SIZE;
                break;
            }
            /* Skip past the used frame instead of inching by one. */
            start = start + i + 1ULL;
        }
    }
    spin_unlock_irqrestore(&pmm_lock, flags);
    return result;
}

void pmm_free_frame(uint64_t phys)
{
    uint64_t flags;
    if (phys == 0 || (phys % PAGE_SIZE) != 0) return;
    flags = spin_lock_irqsave(&pmm_lock);
    pmm_set(phys / PAGE_SIZE, 0);
    spin_unlock_irqrestore(&pmm_lock, flags);
}

uint64_t pmm_total_frames(void)
{
    return total_frames;
}

uint64_t pmm_free_frames(void)
{
    uint64_t flags;
    uint64_t count;
    flags = spin_lock_irqsave(&pmm_lock);
    count = free_count;
    spin_unlock_irqrestore(&pmm_lock, flags);
    return count;
}

void *phys_to_virt(uint64_t phys)
{
    return (void *)(uintptr_t)(phys + hhdm_offset);
}

uint64_t virt_to_phys(const void *virt)
{
    uint64_t addr = (uint64_t)(uintptr_t)virt;
    /* MMIO window is not HHDM — refuse rather than mis-translate. */
    if (addr >= 0xffffd00000000000ULL && addr < 0xffffe00000000000ULL) {
        return 0;
    }
    if (kernel_size != 0 && addr >= kernel_virt_base &&
        addr < kernel_virt_base + kernel_size) {
        return kernel_phys_base + (addr - kernel_virt_base);
    }
    if (hhdm_offset != 0 && addr >= hhdm_offset) {
        return addr - hhdm_offset;
    }
    return 0;
}
