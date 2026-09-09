#include "checksum.h"

uint16_t internet_checksum(const void *data, uint16_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;
    uint16_t i;

    if (bytes == NULL)
    {
        return 0xFFFFU;
    }

    for (i = 0; i + 1U < length; i += 2U)
    {
        sum += (uint32_t)(((uint16_t)bytes[i] << 8) | (uint16_t)bytes[i + 1U]);
    }

    if ((length & 1U) != 0U)
    {
        sum += (uint32_t)((uint16_t)bytes[length - 1U] << 8);
    }

    while ((sum >> 16) != 0U)
    {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}
