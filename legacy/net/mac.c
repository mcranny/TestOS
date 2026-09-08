#include "mac.h"

void mac_copy(mac_addr_t *dst, const mac_addr_t *src)
{
    uint32_t i;

    if (dst == NULL || src == NULL)
    {
        return;
    }

    for (i = 0; i < 6U; i++)
    {
        dst->bytes[i] = src->bytes[i];
    }
}

int mac_equals(const mac_addr_t *a, const mac_addr_t *b)
{
    uint32_t i;

    if (a == NULL || b == NULL)
    {
        return 0;
    }

    for (i = 0; i < 6U; i++)
    {
        if (a->bytes[i] != b->bytes[i])
        {
            return 0;
        }
    }

    return 1;
}

int mac_is_broadcast(const mac_addr_t *mac)
{
    uint32_t i;

    if (mac == NULL)
    {
        return 0;
    }

    for (i = 0; i < 6U; i++)
    {
        if (mac->bytes[i] != 0xFFU)
        {
            return 0;
        }
    }

    return 1;
}

void mac_set_broadcast(mac_addr_t *mac)
{
    uint32_t i;

    if (mac == NULL)
    {
        return;
    }

    for (i = 0; i < 6U; i++)
    {
        mac->bytes[i] = 0xFFU;
    }
}

void mac_format(const mac_addr_t *mac, char *out)
{
    static const char hex_digits[] = "0123456789abcdef";
    uint32_t i;
    uint32_t pos = 0;

    if (mac == NULL || out == NULL)
    {
        return;
    }

    for (i = 0; i < 6U; i++)
    {
        out[pos++] = hex_digits[(mac->bytes[i] >> 4) & 0x0FU];
        out[pos++] = hex_digits[mac->bytes[i] & 0x0FU];
        if (i < 5U)
        {
            out[pos++] = ':';
        }
    }

    out[pos] = '\0';
}
