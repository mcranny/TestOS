#ifndef TESTOS_UEFI_NET_DNS_H
#define TESTOS_UEFI_NET_DNS_H

#include "types.h"
#include "ipv4.h"

void dns_init(void);

/* Resolve hostname to IPv4 A record. Returns 1 on success. */
int dns_resolve(const char *hostname, ipv4_addr_t *out);

/* Polling variant used during boot/tests with explicit timeout. */
int dns_resolve_timeout(const char *hostname, ipv4_addr_t *out, uint32_t timeout_ticks);

#endif
