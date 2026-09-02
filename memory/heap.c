#include "heap.h"
#include "log.h"
#include "memory.h"
#include "pmm.h"

#define HEAP_MAGIC        0x48454150U /* 'HEAP' */
#define HEAP_FOOTER_MAGIC 0xF007F007U

typedef struct heap_block
{
    uint32_t magic;
    size_t size;
    int free;
    struct heap_block *next;
} heap_block_t;

static uint8_t *heap_start;
static uint8_t *heap_end;
static heap_block_t *head_block;
static uint32_t heap_total_bytes;
static uint32_t heap_used_bytes;

static size_t heap_align_payload(size_t size)
{
    size_t aligned = size;

    if (aligned < HEAP_MIN_BLOCK_SIZE)
    {
        aligned = HEAP_MIN_BLOCK_SIZE;
    }

    if (aligned % HEAP_MIN_BLOCK_SIZE != 0)
    {
        aligned += HEAP_MIN_BLOCK_SIZE - (aligned % HEAP_MIN_BLOCK_SIZE);
    }

    return aligned;
}

static size_t heap_block_bytes(size_t payload)
{
    return sizeof(heap_block_t) + payload + sizeof(uint32_t);
}

static uint32_t *heap_footer_ptr(heap_block_t *block)
{
    return (uint32_t *)((uint8_t *)block + sizeof(heap_block_t) + block->size);
}

static void heap_write_footer(heap_block_t *block)
{
    *heap_footer_ptr(block) = HEAP_FOOTER_MAGIC;
}

static int heap_check_block(heap_block_t *block)
{
    uint8_t *block_bytes = (uint8_t *)block;
    uint8_t *footer_end;

    if (block_bytes < heap_start || block_bytes >= heap_end)
    {
        return 0;
    }

    if (block->magic != HEAP_MAGIC)
    {
        return 0;
    }

    footer_end = (uint8_t *)heap_footer_ptr(block) + sizeof(uint32_t);

    if (footer_end > heap_end)
    {
        return 0;
    }

    if (!block->free && *heap_footer_ptr(block) != HEAP_FOOTER_MAGIC)
    {
        return 0;
    }

    return 1;
}

static void heap_split_block(heap_block_t *block, size_t payload)
{
    heap_block_t *new_block;
    size_t remaining;
    size_t used = heap_block_bytes(payload);

    if (heap_block_bytes(block->size) < used + heap_block_bytes(HEAP_MIN_BLOCK_SIZE))
    {
        return;
    }

    remaining = heap_block_bytes(block->size) - used;
    new_block = (heap_block_t *)((uint8_t *)block + used);
    new_block->magic = HEAP_MAGIC;
    new_block->size = remaining - sizeof(heap_block_t) - sizeof(uint32_t);
    new_block->free = 1;
    new_block->next = block->next;
    heap_write_footer(new_block);

    block->size = payload;
    block->next = new_block;
    heap_write_footer(block);
}

static void heap_coalesce(void)
{
    heap_block_t *block = head_block;

    while (block != NULL && block->next != NULL)
    {
        if (block->free && block->next->free)
        {
            block->size = heap_block_bytes(block->size) +
                          heap_block_bytes(block->next->size) -
                          sizeof(heap_block_t) -
                          sizeof(uint32_t);
            block->next = block->next->next;
            heap_write_footer(block);
            continue;
        }

        block = block->next;
    }
}

void heap_initialize(void)
{
    uint32_t heap_base = pmm_get_heap_start();
    uint32_t heap_size = pmm_get_heap_size();

    heap_start = NULL;
    heap_end = NULL;
    head_block = NULL;
    heap_total_bytes = 0;
    heap_used_bytes = 0;

    if (heap_base == 0 ||
        heap_size < heap_block_bytes(HEAP_MIN_BLOCK_SIZE))
    {
        return;
    }

    heap_start = (uint8_t *)heap_base;
    heap_end = heap_start + heap_size;
    heap_total_bytes = heap_size;

    head_block = (heap_block_t *)heap_start;
    head_block->magic = HEAP_MAGIC;
    head_block->size = heap_total_bytes - sizeof(heap_block_t) - sizeof(uint32_t);
    head_block->free = 1;
    head_block->next = NULL;
    heap_write_footer(head_block);
}

void *kmalloc(size_t size)
{
    heap_block_t *block;
    size_t payload;

    if (size == 0 || head_block == NULL)
    {
        return NULL;
    }

    payload = heap_align_payload(size);

    for (block = head_block; block != NULL; block = block->next)
    {
        if (!heap_check_block(block))
        {
            panic("MEM", "HEAP CORRUPTION DETECTED");
        }

        if (!block->free || block->size < payload)
        {
            continue;
        }

        block->free = 0;
        heap_split_block(block, payload);
        heap_write_footer(block);
        heap_used_bytes += (uint32_t)heap_block_bytes(block->size);
        return (uint8_t *)block + sizeof(heap_block_t);
    }

    return NULL;
}

void kfree(void *pointer)
{
    heap_block_t *block;

    if (pointer == NULL || head_block == NULL)
    {
        return;
    }

    block = (heap_block_t *)((uint8_t *)pointer - sizeof(heap_block_t));

    if ((uint8_t *)block < heap_start || (uint8_t *)block >= heap_end)
    {
        return;
    }

    if (!heap_check_block(block) || block->free)
    {
        if (block->free)
        {
            return;
        }

        panic("MEM", "HEAP CORRUPTION DETECTED");
    }

    block->free = 1;

    if (heap_used_bytes >= heap_block_bytes(block->size))
    {
        heap_used_bytes -= (uint32_t)heap_block_bytes(block->size);
    }
    else
    {
        heap_used_bytes = 0;
    }

    heap_coalesce();
}

uint32_t heap_get_start(void)
{
    return (uint32_t)heap_start;
}

uint32_t heap_get_end(void)
{
    return (uint32_t)heap_end;
}

uint32_t heap_get_total_bytes(void)
{
    return heap_total_bytes;
}

uint32_t heap_get_used_bytes(void)
{
    return heap_used_bytes;
}

uint32_t heap_get_free_bytes(void)
{
    if (heap_used_bytes >= heap_total_bytes)
    {
        return 0;
    }

    return heap_total_bytes - heap_used_bytes;
}
