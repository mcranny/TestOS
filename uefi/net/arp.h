#ifndef ARP_H
#define ARP_H

#include "types.h"
#include "mac.h"
#include "ipv4.h"

#define ARP_HTYPE_ETHERNET 1U
#define ARP_PTYPE_IPV4     0x0800U
#define ARP_HLEN           6U
#define ARP_PLEN           4U
#define ARP_OP_REQUEST     1U
#define ARP_OP_REPLY       2U
#define ARP_PACKET_LEN     28U
#define ARP_CACHE_SIZE     16U
#define ARP_PENDING_MAX    4U

/* QEMU user-net defaults used until DHCP succeeds. */
#define ARP_LOCAL_IP_DEFAULT   IPV4_ADDR(10, 0, 2, 15)
#define ARP_GATEWAY_IP_DEFAULT IPV4_ADDR(10, 0, 2, 2)

typedef struct
{
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t oper;
    uint8_t sha[6];
    uint8_t spa[4];
    uint8_t tha[6];
    uint8_t tpa[4];
} __attribute__((packed)) arp_packet_t;

typedef struct
{
    ipv4_addr_t ip;
    mac_addr_t mac;
    uint32_t age_ticks;
    int valid;
} arp_cache_entry_t;

void arp_set_local_ip(ipv4_addr_t ip);
ipv4_addr_t arp_get_local_ip(void);

int arp_lookup(ipv4_addr_t ip, mac_addr_t *mac_out);
void arp_insert(ipv4_addr_t ip, const mac_addr_t *mac);
int arp_delete(ipv4_addr_t ip);
void arp_age_tick(void);

uint32_t arp_cache_count(void);
int arp_cache_get(uint32_t index, arp_cache_entry_t *out);

int arp_request(ipv4_addr_t target_ip);
void arp_input(const uint8_t *payload, uint16_t length, const mac_addr_t *src_mac);

/* Hold one IPv4 frame until next-hop ARP resolves; returns 1 if queued. */
int arp_queue_packet(ipv4_addr_t next_hop, const void *frame, uint16_t length);

/* Boot helper: ARP who-has for the configured gateway. */
void arp_probe_gateway(void);

#endif
