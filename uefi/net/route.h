#ifndef TESTOS_UEFI_NET_ROUTE_H
#define TESTOS_UEFI_NET_ROUTE_H

#include "types.h"
#include "ipv4.h"
#include "netif.h"

typedef struct
{
    ipv4_addr_t network;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway; /* 0 = on-link */
    netif_t *nif;
    int valid;
} route_entry_t;

#define ROUTE_MAX 8U

void route_init(void);
void route_set_defaults_from_netif(netif_t *nif);
int route_add(ipv4_addr_t network, ipv4_addr_t netmask, ipv4_addr_t gateway, netif_t *nif);
void route_set_default_gateway(ipv4_addr_t gateway, netif_t *nif);
ipv4_addr_t route_get_default_gateway(void);

/* Resolve next-hop for destination. Returns 1 on success. */
int route_lookup(ipv4_addr_t dst, ipv4_addr_t *next_hop_out, netif_t **nif_out);

uint32_t route_count(void);
const route_entry_t *route_get_index(uint32_t index);

#endif
