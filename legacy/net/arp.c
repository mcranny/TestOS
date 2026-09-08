#include "arp.h"
#include "ethernet.h"
#include "e1000.h"
#include "log.h"
#include "memory.h"

typedef struct
{
    ipv4_addr_t ip;
    mac_addr_t mac;
    int valid;
} arp_cache_entry_t;

static arp_cache_entry_t arp_cache[ARP_CACHE_SIZE];
static ipv4_addr_t arp_local_ip = ARP_LOCAL_IP_DEFAULT;

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

    if (prefix != NULL)
    {
        for (i = 0; prefix[i] != '\0' && pos < sizeof(line) - 1U; i++)
        {
            line[pos++] = prefix[i];
        }
    }

    for (i = 0; ip_str[i] != '\0' && pos < sizeof(line) - 1U; i++)
    {
        line[pos++] = ip_str[i];
    }

    line[pos] = '\0';
    klog(KLOG_INFO, "ARP", line);
}

static void arp_log_mac(const char *prefix, const mac_addr_t *mac)
{
    char line[48];
    char mac_str[18];
    uint32_t i;
    uint32_t pos = 0;

    if (mac == NULL)
    {
        return;
    }

    mac_format(mac, mac_str);

    if (prefix != NULL)
    {
        for (i = 0; prefix[i] != '\0' && pos < sizeof(line) - 1U; i++)
        {
            line[pos++] = prefix[i];
        }
    }

    for (i = 0; mac_str[i] != '\0' && pos < sizeof(line) - 1U; i++)
    {
        line[pos++] = mac_str[i];
    }

    line[pos] = '\0';
    klog(KLOG_INFO, "ARP", line);
}

void arp_set_local_ip(ipv4_addr_t ip)
{
    arp_local_ip = ip;
}

ipv4_addr_t arp_get_local_ip(void)
{
    return arp_local_ip;
}

int arp_lookup(ipv4_addr_t ip, mac_addr_t *mac_out)
{
    uint32_t i;

    if (mac_out == NULL)
    {
        return 0;
    }

    for (i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && arp_cache[i].ip == ip)
        {
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

    if (mac == NULL || ip == 0U)
    {
        return;
    }

    for (i = 0; i < ARP_CACHE_SIZE; i++)
    {
        if (arp_cache[i].valid && arp_cache[i].ip == ip)
        {
            mac_copy(&arp_cache[i].mac, mac);
            return;
        }

        if (!arp_cache[i].valid && free_slot < 0)
        {
            free_slot = (int)i;
        }
    }

    if (free_slot < 0)
    {
        /* Simple overwrite of slot 0 when full. */
        free_slot = 0;
    }

    arp_cache[free_slot].ip = ip;
    mac_copy(&arp_cache[free_slot].mac, mac);
    arp_cache[free_slot].valid = 1;
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

    if (eth_dst == NULL || sha == NULL || tha == NULL)
    {
        return 0;
    }

    memset(packet, 0, sizeof(packet));
    arp_write_be16(&packet[0], ARP_HTYPE_ETHERNET);
    arp_write_be16(&packet[2], ARP_PTYPE_IPV4);
    packet[4] = ARP_HLEN;
    packet[5] = ARP_PLEN;
    arp_write_be16(&packet[6], oper);

    for (i = 0; i < 6U; i++)
    {
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
    mac_addr_t local_mac;
    const mac_address_t *nic_mac;
    uint32_t i;

    if (target_ip == 0U)
    {
        return 0;
    }

    nic_mac = e1000_get_mac();
    if (nic_mac == NULL)
    {
        return 0;
    }

    for (i = 0; i < 6U; i++)
    {
        local_mac.bytes[i] = nic_mac->bytes[i];
        zero_mac.bytes[i] = 0;
    }

    mac_set_broadcast(&broadcast);
    arp_log_ip("Requesting IP ", target_ip);

    return arp_send_packet(
        &broadcast,
        ARP_OP_REQUEST,
        &local_mac,
        arp_local_ip,
        &zero_mac,
        target_ip
    );
}

static int arp_send_reply(
    const mac_addr_t *dst_mac,
    ipv4_addr_t dst_ip
)
{
    mac_addr_t local_mac;
    const mac_address_t *nic_mac;
    uint32_t i;

    nic_mac = e1000_get_mac();
    if (nic_mac == NULL || dst_mac == NULL)
    {
        return 0;
    }

    for (i = 0; i < 6U; i++)
    {
        local_mac.bytes[i] = nic_mac->bytes[i];
    }

    klog(KLOG_INFO, "ARP", "Sending reply");
    arp_log_ip("Reply target IP ", dst_ip);
    arp_log_mac("Reply target MAC ", dst_mac);

    return arp_send_packet(
        dst_mac,
        ARP_OP_REPLY,
        &local_mac,
        arp_local_ip,
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

    if (payload == NULL || length < ARP_PACKET_LEN)
    {
        return;
    }

    htype = arp_read_be16(&payload[0]);
    ptype = arp_read_be16(&payload[2]);
    oper = arp_read_be16(&payload[6]);

    if (htype != ARP_HTYPE_ETHERNET ||
        ptype != ARP_PTYPE_IPV4 ||
        payload[4] != ARP_HLEN ||
        payload[5] != ARP_PLEN)
    {
        return;
    }

    if (oper != ARP_OP_REQUEST && oper != ARP_OP_REPLY)
    {
        return;
    }

    for (i = 0; i < 6U; i++)
    {
        sender_mac.bytes[i] = payload[8U + i];
    }

    sender_ip = ipv4_addr_from_bytes(&payload[14]);
    target_ip = ipv4_addr_from_bytes(&payload[24]);

    /* Learn the sender mapping from any valid ARP traffic. */
    arp_insert(sender_ip, &sender_mac);

    if (oper == ARP_OP_REPLY)
    {
        klog(KLOG_INFO, "ARP", "Reply received");
        arp_log_ip("IP = ", sender_ip);
        arp_log_mac("MAC = ", &sender_mac);
        return;
    }

    /* ARP request: answer if they want our IP. */
    if (target_ip == arp_local_ip)
    {
        klog(KLOG_INFO, "ARP", "Request for local IP");
        (void)arp_send_reply(&sender_mac, sender_ip);
    }
}

void arp_probe_gateway(void)
{
    (void)arp_request(ARP_GATEWAY_IP_DEFAULT);
}
