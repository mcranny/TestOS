#include "pmm.h"
#include "memory.h"
#include "memory_map.h"

extern char _kernel_end;
extern char stack_top;

static uint8_t *frame_bitmap;
static uint32_t total_frames;
static uint32_t free_frames;
static uint32_t bitmap_bytes;
static uint32_t heap_start_address;
static uint32_t heap_reserved_bytes;

static int pmm_frame_in_range(uint32_t frame)
{
    return frame < total_frames;
}

static int pmm_test_frame(uint32_t frame)
{
    uint32_t byte_index;
    uint32_t bit_index;

    if (!pmm_frame_in_range(frame))
    {
        return 1;
    }

    byte_index = frame / 8U;
    bit_index = frame % 8U;

    return (frame_bitmap[byte_index] >> bit_index) & 1U;
}

static void pmm_set_frame(uint32_t frame, int used)
{
    uint32_t byte_index;
    uint32_t bit_index;
    int was_free;

    if (!pmm_frame_in_range(frame))
    {
        return;
    }

    byte_index = frame / 8U;
    bit_index = frame % 8U;
    was_free = !pmm_test_frame(frame);

    if (used)
    {
        frame_bitmap[byte_index] |= (uint8_t)(1U << bit_index);
        if (was_free && free_frames > 0)
        {
            free_frames--;
        }
    }
    else
    {
        frame_bitmap[byte_index] &= (uint8_t)~(1U << bit_index);
        if (!was_free)
        {
            free_frames++;
        }
    }
}

static void pmm_mark_range(uint32_t physical_address, uint32_t length, int used)
{
    uint32_t start_frame;
    uint32_t end_frame;
    uint32_t frame;

    if (physical_address >= PMM_MAX_ADDRESS)
    {
        return;
    }

    if (physical_address + length > PMM_MAX_ADDRESS)
    {
        length = PMM_MAX_ADDRESS - physical_address;
    }

    start_frame = physical_address / PAGE_SIZE;
    end_frame =
        ALIGN_UP(physical_address + length, PAGE_SIZE) / PAGE_SIZE;

    if (end_frame > total_frames)
    {
        end_frame = total_frames;
    }

    for (frame = start_frame; frame < end_frame; frame++)
    {
        pmm_set_frame(frame, used);
    }
}

void pmm_mark_used(uint32_t physical_address, uint32_t length)
{
    pmm_mark_range(physical_address, length, 1);
}

void pmm_mark_free(uint32_t physical_address, uint32_t length)
{
    pmm_mark_range(physical_address, length, 0);
}

void pmm_initialize(void)
{
    uint32_t index;

    total_frames = PMM_MAX_ADDRESS / PAGE_SIZE;
    bitmap_bytes = ALIGN_UP(total_frames / 8U, PAGE_SIZE);
    frame_bitmap = (uint8_t *)ALIGN_UP((uint32_t)&_kernel_end, PAGE_SIZE);

    memset(frame_bitmap, 0xFF, bitmap_bytes);
    free_frames = 0;

    for (index = 0; index < memory_map_get_region_count(); index++)
    {
        const memory_region_t *region = memory_map_get_region(index);

        if (region == NULL || region->type != MULTIBOOT_MEMORY_AVAILABLE)
        {
            continue;
        }

        if (region->base >= PMM_MAX_ADDRESS)
        {
            continue;
        }

        pmm_mark_free(
            (uint32_t)region->base,
            (uint32_t)region->length
        );
    }

    pmm_mark_used(0, 0x00100000);
    pmm_mark_used(
        KERNEL_LOAD_ADDR,
        (uint32_t)&_kernel_end - KERNEL_LOAD_ADDR
    );
    pmm_mark_used((uint32_t)frame_bitmap, bitmap_bytes);
    pmm_mark_used((uint32_t)&stack_top - 16384, 16384);
    pmm_mark_used(0x000B8000, 0x00008000);

    heap_start_address =
        ALIGN_UP((uint32_t)frame_bitmap + bitmap_bytes, PAGE_SIZE);
    heap_reserved_bytes = HEAP_RESERVED_PAGES * PAGE_SIZE;
    pmm_mark_used(heap_start_address, heap_reserved_bytes);
}

uint32_t pmm_alloc_frame(void)
{
    uint32_t frame;

    for (frame = 0; frame < total_frames; frame++)
    {
        if (pmm_test_frame(frame))
        {
            continue;
        }

        pmm_set_frame(frame, 1);
        return frame * PAGE_SIZE;
    }

    return 0;
}

uint32_t pmm_alloc_contiguous(uint32_t page_count)
{
    uint32_t start_frame;
    uint32_t offset;
    uint32_t end_limit;

    if (page_count == 0 || page_count > total_frames)
    {
        return 0;
    }

    end_limit = total_frames - page_count + 1U;

    for (start_frame = 0; start_frame < end_limit; start_frame++)
    {
        for (offset = 0; offset < page_count; offset++)
        {
            if (pmm_test_frame(start_frame + offset))
            {
                break;
            }
        }

        if (offset < page_count)
        {
            continue;
        }

        for (offset = 0; offset < page_count; offset++)
        {
            pmm_set_frame(start_frame + offset, 1);
        }

        return start_frame * PAGE_SIZE;
    }

    return 0;
}

void pmm_free_frame(uint32_t physical_address)
{
    uint32_t frame = physical_address / PAGE_SIZE;

    if (physical_address == 0 || !pmm_frame_in_range(frame))
    {
        return;
    }

    pmm_set_frame(frame, 0);
}

uint32_t pmm_get_total_frames(void)
{
    return total_frames;
}

uint32_t pmm_get_free_frames(void)
{
    return free_frames;
}

uint32_t pmm_get_bitmap_address(void)
{
    return (uint32_t)frame_bitmap;
}

uint32_t pmm_get_bitmap_size(void)
{
    return bitmap_bytes;
}

uint32_t pmm_get_heap_start(void)
{
    return heap_start_address;
}

uint32_t pmm_get_heap_size(void)
{
    return heap_reserved_bytes;
}
