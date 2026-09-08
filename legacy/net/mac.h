#ifndef MAC_H
#define MAC_H

#include "types.h"

typedef struct
{
    uint8_t bytes[6];
} mac_addr_t;

void mac_copy(mac_addr_t *dst, const mac_addr_t *src);
int mac_equals(const mac_addr_t *a, const mac_addr_t *b);
int mac_is_broadcast(const mac_addr_t *mac);
void mac_set_broadcast(mac_addr_t *mac);
void mac_format(const mac_addr_t *mac, char *out);

#endif
