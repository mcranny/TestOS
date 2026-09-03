#include "ipv4.h"
#include "arp.h"
#include "checksum.h"
#include "ethernet.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "log.h"
#include "memory.h"

static uint16_t ipv4_next_id = 1;

static uint16_t ipv4_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void ipv4_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)((value >> 8) & 0xFFU);
    p[1] = (uint8_t)(value & 0xFFU);
}

void ipv4_addr_to_bytes(ipv4_addr_t ip, uint8_t out[4])
{
    if (out == NULL)
    {
        return;
    }

    out[0] = (uint8_t)((ip >> 24) & 0xFFU);
    out[1] = (uint8_t)((ip >> 16) & 0xFFU);
    out[2] = (uint8_t)((ip >> 8) & 0xFFU);
    out[3] = (uint8_t)(ip & 0xFFU);
}

ipv4_addr_t ipv4_addr_from_bytes(const uint8_t in[4])
{
    if (in == NULL)
    {
        return 0;
    }

    return IPV4_ADDR(in[0], in[1], in[2], in[3]);
}

void ipv4_addr_format(ipv4_addr_t ip, char *out)
{
    static const char digits[] = "0123456789";
    uint8_t octets[4];
    uint32_t i;
    uint32_t pos = 0;

    if (out == NULL)
    {
        return;
    }

    ipv4_addr_to_bytes(ip, octets);

    for (i = 0; i < 4U; i++)
    {
        uint8_t v = octets[i];
        char tmp[3];
        int tlen = 0;
        int j;

        do
        {
            tmp[tlen++] = digits[v % 10U];
            v = (uint8_t)(v / 10U);
        } while (v != 0U);

        for (j = tlen - 1; j >= 0; j--)
        {
            out[pos++] = tmp[j];
        }

        if (i < 3U)
        {
            out[pos++] = '.';
        }
    }

    out[pos] = '\0';
}

void ipv4_input(const uint8_t *payload, uint16_t length, const mac_addr_t *src_mac)
{
    uint8_t version;
    uint8_t ihl;
    uint16_t hdr_len;
    uint16_t total_length;
    uint16_t flags_fragment;
    ipv4_addr_t src_ip;
    ipv4_addr_t dst_ip;
    uint8_t protocol;
    const uint8_t *l4;
    uint16_t l4_len;

    (void)src_mac;

    if (payload == NULL || length < IPV4_HDR_MIN_LEN)
    {
        return;
    }

    version = (uint8_t)(payload[0] >> 4);
    ihl = (uint8_t)(payload[0] & 0x0FU);
    if (version != IPV4_VERSION || ihl < IPV4_IHL_MIN)
    {
        return;
    }

    hdr_len = (uint16_t)(ihl * 4U);
    if (hdr_len > length)
    {
        return;
    }

    total_length = ipv4_read_be16(&payload[2]);
    if (total_length < hdr_len || total_length > length)
    {
        return;
    }

    flags_fragment = ipv4_read_be16(&payload[6]);
    if ((flags_fragment & 0x3FFFU) != 0U)
    {
        return;
    }

    if (internet_checksum(payload, hdr_len) != 0U)
    {
        return;
    }

    src_ip = ipv4_addr_from_bytes(&payload[12]);
    dst_ip = ipv4_addr_from_bytes(&payload[16]);
    if (dst_ip != arp_get_local_ip())
    {
        return;
    }

    protocol = payload[9];
    l4 = payload + hdr_len;
    l4_len = (uint16_t)(total_length - hdr_len);

    klog(KLOG_INFO, "IPV4", "packet");
    klog_uint(KLOG_INFO, "IPV4", "Protocol = ", protocol);
    klog_uint(KLOG_INFO, "IPV4", "Length = ", total_length);

    if (protocol == IPV4_PROTO_ICMP)
    {
        icmp_input(l4, l4_len, src_ip, dst_ip);
    }
    else if (protocol == IPV4_PROTO_UDP)
    {
        udp_input(l4, l4_len, src_ip, dst_ip);
    }
    else if (protocol == IPV4_PROTO_TCP)
    {
        tcp_input(l4, l4_len, src_ip, dst_ip);
    }
}

int ipv4_send(
    ipv4_addr_t dst,
    uint8_t protocol,
    const void *payload,
    uint16_t payload_len
)
{
    uint8_t packet[ETH_MAX_PAYLOAD];
    mac_addr_t dst_mac;
    uint16_t total;
    uint16_t checksum;
    uint16_t id;

    if (dst == 0U || (payload == NULL && payload_len != 0U))
    {
        return 0;
    }

    if (payload_len > (ETH_MAX_PAYLOAD - IPV4_HDR_MIN_LEN))
    {
        return 0;
    }

    if (!arp_lookup(dst, &dst_mac))
    {
        (void)arp_request(dst);
        return 0;
    }

    total = (uint16_t)(IPV4_HDR_MIN_LEN + payload_len);
    memset(packet, 0, IPV4_HDR_MIN_LEN);

    packet[0] = (uint8_t)((IPV4_VERSION << 4) | IPV4_IHL_MIN);
    packet[1] = 0;
    ipv4_write_be16(&packet[2], total);

    id = ipv4_next_id++;
    if (ipv4_next_id == 0U)
    {
        ipv4_next_id = 1U;
    }
    ipv4_write_be16(&packet[4], id);
    ipv4_write_be16(&packet[6], 0);

    packet[8] = IPV4_TTL_DEFAULT;
    packet[9] = protocol;
    ipv4_write_be16(&packet[10], 0);

    ipv4_addr_to_bytes(arp_get_local_ip(), &packet[12]);
    ipv4_addr_to_bytes(dst, &packet[16]);

    checksum = internet_checksum(packet, IPV4_HDR_MIN_LEN);
    ipv4_write_be16(&packet[10], checksum);

    if (payload_len > 0U)
    {
        memcpy(&packet[IPV4_HDR_MIN_LEN], payload, payload_len);
    }

    klog(KLOG_INFO, "IPV4", "Sending packet");
    klog_uint(KLOG_INFO, "IPV4", "Protocol = ", protocol);
    klog_uint(KLOG_INFO, "IPV4", "Length = ", total);

    return ethernet_send(&dst_mac, ETHERTYPE_IPV4, packet, total);
}
