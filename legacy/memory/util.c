#include "memory.h"

void *memset(void *destination, int value, size_t count)
{
    uint8_t *bytes = (uint8_t *)destination;

    while (count > 0)
    {
        *bytes++ = (uint8_t)value;
        count--;
    }

    return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *dest_bytes = (uint8_t *)destination;
    const uint8_t *source_bytes = (const uint8_t *)source;

    while (count > 0)
    {
        *dest_bytes++ = *source_bytes++;
        count--;
    }

    return destination;
}
