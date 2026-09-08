#include "types.h"

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = dest;
    const uint8_t *s = src;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memset(void *dest, int value, size_t n)
{
    uint8_t *d = dest;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = (uint8_t)value;
    }
    return dest;
}
