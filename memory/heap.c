#include "heap.h"
#include "memory.h"
#include "pmm.h"

typedef struct heap_block
{
    size_t size;
    int free;
    struct heap_block *next;
} heap_block_t;

static uint8_t *heap_start;
static uint8_t *heap_end;
static heap_block_t *head_block;
static uint32_t heap_total_bytes;
static uint32_t heap_used_bytes;

static size_t heap_align_size(size_t size)
{
    size_t aligned = size + sizeof(heap_block_t);

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

static void heap_split_block(heap_block_t *block, size_t size)
{
    heap_block_t *new_block;

    if (block->size < size + sizeof(heap_block_t) + HEAP_MIN_BLOCK_SIZE)
    {
        return;
    }

    new_block = (heap_block_t *)((uint8_t *)block + sizeof(heap_block_t) + size);
    new_block->size = block->size - size - sizeof(heap_block_t);
    new_block->free = 1;
    new_block->next = block->next;
    block->size = size;
    block->next = new_block;
}

static void heap_coalesce(void)
{
    heap_block_t *block = head_block;

    while (block != NULL && block->next != NULL)
    {
        if (block->free && block->next->free)
        {
            block->size += sizeof(heap_block_t) + block->next->size;
            block->next = block->next->next;
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

    if (heap_base == 0 || heap_size < sizeof(heap_block_t) + HEAP_MIN_BLOCK_SIZE)
    {
        return;
    }

    heap_start = (uint8_t *)heap_base;
    heap_end = heap_start + heap_size;
    heap_total_bytes = heap_size;

    head_block = (heap_block_t *)heap_start;
    head_block->size = heap_total_bytes - sizeof(heap_block_t);
    head_block->free = 1;
    head_block->next = NULL;
}

void *kmalloc(size_t size)
{
    heap_block_t *block;
    size_t aligned_size;

    if (size == 0 || head_block == NULL)
    {
        return NULL;
    }

    aligned_size = heap_align_size(size);

    for (block = head_block; block != NULL; block = block->next)
    {
        if (!block->free || block->size < aligned_size)
        {
            continue;
        }

        block->free = 0;
        heap_split_block(block, aligned_size);
        heap_used_bytes += (uint32_t)(block->size + sizeof(heap_block_t));
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

    if (block->free)
    {
        return;
    }

    block->free = 1;

    if (heap_used_bytes >= block->size + sizeof(heap_block_t))
    {
        heap_used_bytes -= (uint32_t)(block->size + sizeof(heap_block_t));
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
