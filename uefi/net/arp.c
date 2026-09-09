#include "arp.h"
#include "ethernet.h"
#include "netif.h"
#include "route.h"
#include "platform.h"
#include "lib/string.h"

typedef struct
{
    ipv4_addr_t next_hop;
    uint16_t length;
    int valid;
    uint8_t frame[ETH_MAX_FRAME];
} arp_pending_t;

static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static arp_pending_t arp_pending[ARP_PENDING_MAX];
static ipv4_addr_t arp_local_ip = ARP_LOCAL_IP_DEFAULT;

#define ARP_CACHE_TTL_TICKS (60U * TIMER_FREQUENCY)

static uint16_t arp_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void arp_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)((value >> 8) & 0xFFU);
    p[1] = (uint8_t)(value & 0xFFU);
}

static void arp_log_ip(const char *prefix, ipv4_addr_t ip)
{
    char line[48];
    char ip_str[16];
    uint32_t i;
    uint32_t pos = 0;

    ipv4_addr_format(ip, ip_str);

    if (prefix != NULL) {
        for (i = 0; prefix[i] != '\0' && pos < sizeof(line) - 1U; i++) {
            line[pos++] = prefix[i];
        }
    }
    for (i = 0; ip_str[i] != '\0' && pos < sizeof(line) - 1U; i++) {
        line[pos++] = ip_str[i];
    }
    line[pos] = '\0';
    klog(KLOG_INFO, "ARP", line);
}

static const mac_addr_t *arp_local_mac(void)
{
    const mac_addr_t *mac = netif_get_mac();
    if (mac != NULL) {
        return mac;
    }
    return NULL;
}

void arp_set_local_ip(ipv4_addr_t ip)
{
    netif_t *nif = netif_get_primary();

    arp_local_ip = ip;
    if (nif != NULL) {
        nif->ip = ip;
    }
}

ipv4_addr_t arp_get_local_ip(void)
{
    ipv4_addr_t ip = netif_get_ip();
    return ip != 0U ? ip : arp_local_ip;
}

int arp_lookup(ipv4_addr_t ip, mac_addr_t *mac_out)
{
    uint32_t i;

    if (mac_out == NULL) {
        return 0;
    }
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            mac_copy(mac_out, &arp_cache[i].mac);
            return 1;
        }
    }
    return 0;
}

void arp_insert(ipv4_addr_t ip, const mac_addr_t *mac)
{
    uint32_t i;
    int free_slot = -1;
    int oldest = -1;
    uint32_t oldest_age = 0;

    if (mac == NULL || ip == 0U) {
        return;
    }

    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            mac_copy(&arp_cache[i].mac, mac);
            arp_cache[i].age_ticks = timer_get_ticks();
            return;
        }
        if (!arp_cache[i].valid && free_slot < 0) {
            free_slot = (int)i;
        }
        /* Track true minimum age_ticks; do not use age==0 as a sentinel. */
        if (arp_cache[i].valid && free_slot < 0 &&
            (oldest < 0 || arp_cache[i].age_ticks < oldest_age)) {
            oldest = (int)i;
            oldest_age = arp_cache[i].age_ticks;
        }
    }

    if (free_slot < 0) {
        free_slot = oldest >= 0 ? oldest : 0;
    }

    arp_cache[free_slot].ip = ip;
    mac_copy(&arp_cache[free_slot].mac, mac);
    arp_cache[free_slot].age_ticks = timer_get_ticks();
    arp_cache[free_slot].valid = 1;
}

int arp_delete(ipv4_addr_t ip)
{
    uint32_t i;

    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            arp_cache[i].valid = 0;
            return 1;
        }
    }
    return 0;
}

void arp_age_tick(void)
{
    uint32_t i;
    uint32_t now = timer_get_ticks();

    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid &&
            (now - arp_cache[i].age_ticks) > ARP_CACHE_TTL_TICKS) {
            arp_cache[i].valid = 0;
        }
    }
}

uint32_t arp_cache_count(void)
{
    uint32_t i;
    uint32_t n = 0;

    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid) {
            n++;
        }
    }
    return n;
}

int arp_cache_get(uint32_t index, arp_cache_entry_t *out)
{
    uint32_t i;
    uint32_t n = 0;

    if (out == NULL) {
        return 0;
    }
    for (i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            continue;
        }
        if (n == index) {
            *out = arp_cache[i];
            return 1;
        }
        n++;
    }
    return 0;
}

static void arp_flush_pending(ipv4_addr_t ip, const mac_addr_t *mac)
{
    uint32_t i;
    uint32_t b;
    netif_t *nif = netif_get_primary();

    if (mac == NULL) {
        return;
    }
    for (i = 0; i < ARP_PENDING_MAX; i++) {
        if (arp_pending[i].valid && arp_pending[i].next_hop == ip) {
            for (b = 0; b < 6U; b++) {
                arp_pending[i].frame[b] = mac->bytes[b];
            }
            if (nif != NULL) {
                (void)netif_transmit(nif, arp_pending[i].frame, arp_pending[i].length);
            }
            arp_pending[i].valid = 0;
        }
    }
}

int arp_queue_packet(ipv4_addr_t next_hop, const void *frame, uint16_t length)
{
    uint32_t i;
    int slot = -1;

    if (frame == NULL || length == 0 || length > ETH_MAX_FRAME || next_hop == 0U) {
        return 0;
    }

    for (i = 0; i < ARP_PENDING_MAX; i++) {
        if (arp_pending[i].valid && arp_pending[i].next_hop == next_hop) {
            slot = (int)i;
            break;
        }
        if (!arp_pending[i].valid && slot < 0) {
            slot = (int)i;
        }
    }
    if (slot < 0) {
        slot = 0;
    }

    arp_pending[slot].next_hop = next_hop;
    arp_pending[slot].length = length;
    memcpy(arp_pending[slot].frame, frame, length);
    arp_pending[slot].valid = 1;
    return 1;
}

static int arp_send_packet(
    const mac_addr_t *eth_dst,
    uint16_t oper,
    const mac_addr_t *sha,
    ipv4_addr_t spa,
    const mac_addr_t *tha,
    ipv4_addr_t tpa
)
{
    uint8_t packet[ARP_PACKET_LEN];
    uint32_t i;

    if (eth_dst == NULL || sha == NULL || tha == NULL) {
        return 0;
    }

    memset(packet, 0, sizeof(packet));
    arp_write_be16(&packet[0], ARP_HTYPE_ETHERNET);
    arp_write_be16(&packet[2], ARP_PTYPE_IPV4);
    packet[4] = ARP_HLEN;
    packet[5] = ARP_PLEN;
    arp_write_be16(&packet[6], oper);

    for (i = 0; i < 6U; i++) {
        packet[8U + i] = sha->bytes[i];
        packet[18U + i] = tha->bytes[i];
    }

    ipv4_addr_to_bytes(spa, &packet[14]);
    ipv4_addr_to_bytes(tpa, &packet[24]);

    return ethernet_send(eth_dst, ETHERTYPE_ARP, packet, ARP_PACKET_LEN);
}

int arp_request(ipv4_addr_t target_ip)
{
    mac_addr_t broadcast;
    mac_addr_t zero_mac;
    const mac_addr_t *local_mac;
    uint32_t i;

    if (target_ip == 0U) {
        return 0;
    }

    local_mac = arp_local_mac();
    if (local_mac == NULL) {
        return 0;
    }

    for (i = 0; i < 6U; i++) {
        zero_mac.bytes[i] = 0;
    }

    mac_set_broadcast(&broadcast);
    arp_log_ip("Requesting IP ", target_ip);

    return arp_send_packet(
        &broadcast,
        ARP_OP_REQUEST,
        local_mac,
        arp_get_local_ip(),
        &zero_mac,
        target_ip
    );
}

static int arp_send_reply(const mac_addr_t *dst_mac, ipv4_addr_t dst_ip)
{
    const mac_addr_t *local_mac;

    local_mac = arp_local_mac();
    if (local_mac == NULL || dst_mac == NULL) {
        return 0;
    }

    return arp_send_packet(
        dst_mac,
        ARP_OP_REPLY,
        local_mac,
        arp_get_local_ip(),
        dst_mac,
        dst_ip
    );
}

void arp_input(const uint8_t *payload, uint16_t length, const mac_addr_t *src_mac)
{
    uint16_t htype;
    uint16_t ptype;
    uint16_t oper;
    mac_addr_t sender_mac;
    ipv4_addr_t sender_ip;
    ipv4_addr_t target_ip;
    uint32_t i;

    (void)src_mac;

    if (payload == NULL || length < ARP_PACKET_LEN) {
        return;
    }

    htype = arp_read_be16(&payload[0]);
    ptype = arp_read_be16(&payload[2]);
    oper = arp_read_be16(&payload[6]);

    if (htype != ARP_HTYPE_ETHERNET ||
        ptype != ARP_PTYPE_IPV4 ||
        payload[4] != ARP_HLEN ||
        payload[5] != ARP_PLEN) {
        return;
    }

    if (oper != ARP_OP_REQUEST && oper != ARP_OP_REPLY) {
        return;
    }

    for (i = 0; i < 6U; i++) {
        sender_mac.bytes[i] = payload[8U + i];
    }

    sender_ip = ipv4_addr_from_bytes(&payload[14]);
    target_ip = ipv4_addr_from_bytes(&payload[24]);

    arp_insert(sender_ip, &sender_mac);
    arp_flush_pending(sender_ip, &sender_mac);

    if (oper == ARP_OP_REPLY) {
        return;
    }

    if (target_ip == arp_get_local_ip()) {
        (void)arp_send_reply(&sender_mac, sender_ip);
    }
}

void arp_probe_gateway(void)
{
    ipv4_addr_t gw = netif_get_gateway();
    if (gw == 0U) {
        gw = ARP_GATEWAY_IP_DEFAULT;
    }
    (void)arp_request(gw);
}
