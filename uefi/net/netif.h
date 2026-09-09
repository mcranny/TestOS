#ifndef TESTOS_UEFI_NET_NETIF_H
#define TESTOS_UEFI_NET_NETIF_H

#include "types.h"
#include "mac.h"
#include "ipv4.h"

#define NETIF_MAX          4U
#define NETIF_NAME_MAX     16U
#define NETIF_DNS_MAX      2U

#define NETIF_FLAG_UP      (1U << 0)
#define NETIF_FLAG_RUNNING (1U << 1)
#define NETIF_FLAG_DHCP    (1U << 2)

typedef struct netif netif_t;

typedef struct
{
    int (*transmit)(netif_t *nif, const void *data, uint16_t length);
    int (*poll)(netif_t *nif);
} netif_ops_t;

struct netif
{
    char name[NETIF_NAME_MAX];
    mac_addr_t mac;
    ipv4_addr_t ip;
    ipv4_addr_t netmask;
    ipv4_addr_t gateway;
    ipv4_addr_t dns[NETIF_DNS_MAX];
    uint32_t flags;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t drop_packets;
    const netif_ops_t *ops;
    void *priv;
    int active;
};

void netif_init(void);
netif_t *netif_register(const char *name, const mac_addr_t *mac, const netif_ops_t *ops, void *priv);
netif_t *netif_get_primary(void);
netif_t *netif_find(const char *name);
uint32_t netif_count(void);
netif_t *netif_get_index(uint32_t index);

void netif_set_addr(netif_t *nif, ipv4_addr_t ip, ipv4_addr_t netmask, ipv4_addr_t gateway);
void netif_set_dns(netif_t *nif, ipv4_addr_t dns0, ipv4_addr_t dns1);
void netif_set_flags(netif_t *nif, uint32_t flags);
void netif_add_flags(netif_t *nif, uint32_t flags);
void netif_clear_flags(netif_t *nif, uint32_t flags);

ipv4_addr_t netif_get_ip(void);
ipv4_addr_t netif_get_netmask(void);
ipv4_addr_t netif_get_gateway(void);
ipv4_addr_t netif_get_dns(uint32_t index);
const mac_addr_t *netif_get_mac(void);

void netif_inc_rx(netif_t *nif);
void netif_inc_tx(netif_t *nif);
void netif_inc_drop(netif_t *nif);

int netif_transmit(netif_t *nif, const void *data, uint16_t length);
int netif_poll(netif_t *nif);

/* Apply QEMU user-net static defaults (used as DHCP fallback). */
void netif_apply_static_defaults(netif_t *nif);

#endif
