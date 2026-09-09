#include "netif.h"
#include "lib/string.h"

#define NETIF_DEFAULT_IP      IPV4_ADDR(10, 0, 2, 15)
#define NETIF_DEFAULT_MASK    IPV4_ADDR(255, 255, 255, 0)
#define NETIF_DEFAULT_GATEWAY IPV4_ADDR(10, 0, 2, 2)
#define NETIF_DEFAULT_DNS     IPV4_ADDR(10, 0, 2, 3)

static netif_t netifs[NETIF_MAX];
static uint32_t netif_used;

void netif_init(void)
{
    memset(netifs, 0, sizeof(netifs));
    netif_used = 0;
}

static void copy_name(char *dst, const char *src)
{
    uint32_t i;

    if (dst == NULL) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    for (i = 0; i + 1U < NETIF_NAME_MAX && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

netif_t *netif_register(const char *name, const mac_addr_t *mac, const netif_ops_t *ops, void *priv)
{
    netif_t *nif;

    if (netif_used >= NETIF_MAX || name == NULL || mac == NULL || ops == NULL) {
        return NULL;
    }

    nif = &netifs[netif_used++];
    memset(nif, 0, sizeof(*nif));
    copy_name(nif->name, name);
    mac_copy(&nif->mac, mac);
    nif->ops = ops;
    nif->priv = priv;
    nif->active = 1;
    nif->flags = NETIF_FLAG_UP | NETIF_FLAG_RUNNING;
    return nif;
}

netif_t *netif_get_primary(void)
{
    uint32_t i;

    for (i = 0; i < netif_used; i++) {
        if (netifs[i].active) {
            return &netifs[i];
        }
    }
    return NULL;
}

static int name_eq(const char *a, const char *b)
{
    uint32_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; ; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
}

netif_t *netif_find(const char *name)
{
    uint32_t i;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < netif_used; i++) {
        if (netifs[i].active && name_eq(netifs[i].name, name)) {
            return &netifs[i];
        }
    }
    return NULL;
}

uint32_t netif_count(void)
{
    return netif_used;
}

netif_t *netif_get_index(uint32_t index)
{
    if (index >= netif_used) {
        return NULL;
    }
    return &netifs[index];
}

void netif_set_addr(netif_t *nif, ipv4_addr_t ip, ipv4_addr_t netmask, ipv4_addr_t gateway)
{
    if (nif == NULL) {
        return;
    }
    nif->ip = ip;
    nif->netmask = netmask;
    nif->gateway = gateway;
}

void netif_set_dns(netif_t *nif, ipv4_addr_t dns0, ipv4_addr_t dns1)
{
    if (nif == NULL) {
        return;
    }
    nif->dns[0] = dns0;
    nif->dns[1] = dns1;
}

void netif_set_flags(netif_t *nif, uint32_t flags)
{
    if (nif != NULL) {
        nif->flags = flags;
    }
}

void netif_add_flags(netif_t *nif, uint32_t flags)
{
    if (nif != NULL) {
        nif->flags |= flags;
    }
}

void netif_clear_flags(netif_t *nif, uint32_t flags)
{
    if (nif != NULL) {
        nif->flags &= ~flags;
    }
}

ipv4_addr_t netif_get_ip(void)
{
    netif_t *nif = netif_get_primary();
    return nif != NULL ? nif->ip : 0;
}

ipv4_addr_t netif_get_netmask(void)
{
    netif_t *nif = netif_get_primary();
    return nif != NULL ? nif->netmask : 0;
}

ipv4_addr_t netif_get_gateway(void)
{
    netif_t *nif = netif_get_primary();
    return nif != NULL ? nif->gateway : 0;
}

ipv4_addr_t netif_get_dns(uint32_t index)
{
    netif_t *nif = netif_get_primary();
    if (nif == NULL || index >= NETIF_DNS_MAX) {
        return 0;
    }
    return nif->dns[index];
}

const mac_addr_t *netif_get_mac(void)
{
    netif_t *nif = netif_get_primary();
    return nif != NULL ? &nif->mac : NULL;
}

void netif_inc_rx(netif_t *nif)
{
    if (nif != NULL) {
        nif->rx_packets++;
    }
}

void netif_inc_tx(netif_t *nif)
{
    if (nif != NULL) {
        nif->tx_packets++;
    }
}

void netif_inc_drop(netif_t *nif)
{
    if (nif != NULL) {
        nif->drop_packets++;
    }
}

int netif_transmit(netif_t *nif, const void *data, uint16_t length)
{
    if (nif == NULL || nif->ops == NULL || nif->ops->transmit == NULL) {
        return 0;
    }
    return nif->ops->transmit(nif, data, length);
}

int netif_poll(netif_t *nif)
{
    if (nif == NULL || nif->ops == NULL || nif->ops->poll == NULL) {
        return 0;
    }
    return nif->ops->poll(nif);
}

void netif_apply_static_defaults(netif_t *nif)
{
    if (nif == NULL) {
        return;
    }
    netif_set_addr(nif, NETIF_DEFAULT_IP, NETIF_DEFAULT_MASK, NETIF_DEFAULT_GATEWAY);
    netif_set_dns(nif, NETIF_DEFAULT_DNS, 0);
    netif_clear_flags(nif, NETIF_FLAG_DHCP);
}
