#include "route.h"
#include "lib/string.h"

static route_entry_t routes[ROUTE_MAX];
static uint32_t route_used;

void route_init(void)
{
    memset(routes, 0, sizeof(routes));
    route_used = 0;
}

static int route_slot(ipv4_addr_t network, ipv4_addr_t netmask)
{
    uint32_t i;

    for (i = 0; i < route_used; i++) {
        if (routes[i].valid && routes[i].network == network && routes[i].netmask == netmask) {
            return (int)i;
        }
    }
    if (route_used >= ROUTE_MAX) {
        return -1;
    }
    return (int)route_used++;
}

int route_add(ipv4_addr_t network, ipv4_addr_t netmask, ipv4_addr_t gateway, netif_t *nif)
{
    int slot = route_slot(network & netmask, netmask);

    if (slot < 0) {
        return 0;
    }
    routes[slot].network = network & netmask;
    routes[slot].netmask = netmask;
    routes[slot].gateway = gateway;
    routes[slot].nif = nif;
    routes[slot].valid = 1;
    return 1;
}

void route_set_default_gateway(ipv4_addr_t gateway, netif_t *nif)
{
    (void)route_add(0, 0, gateway, nif);
}

ipv4_addr_t route_get_default_gateway(void)
{
    uint32_t i;

    for (i = 0; i < route_used; i++) {
        if (routes[i].valid && routes[i].netmask == 0 && routes[i].network == 0) {
            return routes[i].gateway;
        }
    }
    return 0;
}

void route_set_defaults_from_netif(netif_t *nif)
{
    if (nif == NULL) {
        return;
    }
    route_init();
    if (nif->netmask != 0) {
        (void)route_add(nif->ip & nif->netmask, nif->netmask, 0, nif);
    }
    if (nif->gateway != 0) {
        route_set_default_gateway(nif->gateway, nif);
    }
}

int route_lookup(ipv4_addr_t dst, ipv4_addr_t *next_hop_out, netif_t **nif_out)
{
    uint32_t i;
    int best = -1;
    uint32_t best_mask_bits = 0;

    if (dst == 0 || next_hop_out == NULL) {
        return 0;
    }

    for (i = 0; i < route_used; i++) {
        uint32_t bits;
        uint32_t m;

        if (!routes[i].valid) {
            continue;
        }
        if ((dst & routes[i].netmask) != routes[i].network) {
            continue;
        }
        bits = 0;
        m = routes[i].netmask;
        while (m != 0U) {
            bits += m & 1U;
            m >>= 1;
        }
        /* Prefer longer prefixes; for equal (incl. default), keep first. */
        if (best < 0 || bits > best_mask_bits) {
            best = (int)i;
            best_mask_bits = bits;
        }
    }

    if (best < 0) {
        return 0;
    }

    if (routes[best].gateway != 0) {
        *next_hop_out = routes[best].gateway;
    } else {
        *next_hop_out = dst;
    }
    if (nif_out != NULL) {
        *nif_out = routes[best].nif;
    }
    return 1;
}

uint32_t route_count(void)
{
    return route_used;
}

const route_entry_t *route_get_index(uint32_t index)
{
    if (index >= route_used || !routes[index].valid) {
        return NULL;
    }
    return &routes[index];
}
