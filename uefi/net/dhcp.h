#ifndef TESTOS_UEFI_NET_DHCP_H
#define TESTOS_UEFI_NET_DHCP_H

#include "types.h"
#include "netif.h"

void dhcp_init(void);

/* Blocking DHCP discover/request; polls ethernet until bound or timeout_ticks. */
int dhcp_request(netif_t *nif, uint32_t timeout_ticks);

/* Non-blocking renew trigger used by shell `dhcp`. */
int dhcp_renew(netif_t *nif);

int dhcp_is_bound(void);

#endif
