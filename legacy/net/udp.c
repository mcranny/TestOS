#include "udp.h"
#include "arp.h"
#include "checksum.h"
#include "ethernet.h"
#include "ipv4.h"
#include "log.h"
#include "memory.h"

typedef struct
{
    uint16_t port;
    udp_handler_t handler;
    int in_use;
} udp_bind_entry_t;

static udp_bind_entry_t udp_binds[UDP_BIND_MAX];

static void udp_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)((value >> 8) & 0xFFU);
    p[1] = (uint8_t)(value & 0xFFU);
}

static uint16_t udp_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/*
 * Internet checksum over IPv4 pseudo-header + UDP datagram.
 * For TX, checksum field in datagram must be zero.
 * Returns the value to store (never 0 — use 0xFFFF), or for verify
 * returns 0 when the packet checksum is valid.
 */
static uint16_t udp_checksum(
    ipv4_addr_t src_ip,
    ipv4_addr_t dst_ip,
    const uint8_t *udp_packet,
    uint16_t udp_len
)
{
    uint8_t buf[12U + ETH_MAX_PAYLOAD];
    uint16_t total;

    if (udp_packet == NULL || udp_len < UDP_HDR_LEN ||
        udp_len > ETH_MAX_PAYLOAD)
    {
        return 0xFFFFU;
    }

    total = (uint16_t)(12U + udp_len);
    memset(buf, 0, total);

    ipv4_addr_to_bytes(src_ip, &buf[0]);
    ipv4_addr_to_bytes(dst_ip, &buf[4]);
    buf[8] = 0;
    buf[9] = IPV4_PROTO_UDP;
    udp_write_be16(&buf[10], udp_len);
    memcpy(&buf[12], udp_packet, udp_len);

    return internet_checksum(buf, total);
}

static udp_handler_t udp_find_handler(uint16_t port)
{
    uint32_t i;

    for (i = 0; i < UDP_BIND_MAX; i++)
    {
        if (udp_binds[i].in_use && udp_binds[i].port == port)
        {
            return udp_binds[i].handler;
        }
    }

    return NULL;
}

int udp_bind(uint16_t port, udp_handler_t handler)
{
    uint32_t i;
    int free_slot = -1;

    if (port == 0U || handler == NULL)
    {
        return 0;
    }

    for (i = 0; i < UDP_BIND_MAX; i++)
    {
        if (udp_binds[i].in_use && udp_binds[i].port == port)
        {
            return 0;
        }

        if (!udp_binds[i].in_use && free_slot < 0)
        {
            free_slot = (int)i;
        }
    }

    if (free_slot < 0)
    {
        return 0;
    }

    udp_binds[free_slot].port = port;
    udp_binds[free_slot].handler = handler;
    udp_binds[free_slot].in_use = 1;
    return 1;
}

int udp_send(
    ipv4_addr_t dst_ip,
    uint16_t dst_port,
    uint16_t src_port,
    const void *payload,
    uint16_t payload_len
)
{
    uint8_t packet[ETH_MAX_PAYLOAD];
    uint16_t udp_len;
    uint16_t checksum;
    ipv4_addr_t src_ip;

    if (dst_ip == 0U || src_port == 0U || dst_port == 0U)
    {
        return 0;
    }

    if (payload == NULL && payload_len != 0U)
    {
        return 0;
    }

    if (payload_len > (ETH_MAX_PAYLOAD - IPV4_HDR_MIN_LEN - UDP_HDR_LEN))
    {
        return 0;
    }

    udp_len = (uint16_t)(UDP_HDR_LEN + payload_len);
    memset(packet, 0, udp_len);

    udp_write_be16(&packet[0], src_port);
    udp_write_be16(&packet[2], dst_port);
    udp_write_be16(&packet[4], udp_len);
    udp_write_be16(&packet[6], 0);

    if (payload_len > 0U)
    {
        memcpy(&packet[UDP_HDR_LEN], payload, payload_len);
    }

    src_ip = arp_get_local_ip();
    checksum = udp_checksum(src_ip, dst_ip, packet, udp_len);
    if (checksum == 0U)
    {
        checksum = 0xFFFFU;
    }
    udp_write_be16(&packet[6], checksum);

    return ipv4_send(dst_ip, IPV4_PROTO_UDP, packet, udp_len);
}

void udp_input(
    const uint8_t *payload,
    uint16_t length,
    ipv4_addr_t src_ip,
    ipv4_addr_t dst_ip
)
{
    uint16_t udp_len;
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t checksum_field;
    uint16_t data_len;
    const uint8_t *data;
    udp_handler_t handler;

    if (payload == NULL || length < UDP_HDR_LEN)
    {
        return;
    }

    udp_len = udp_read_be16(&payload[4]);
    if (udp_len < UDP_HDR_LEN || udp_len > length)
    {
        return;
    }

    checksum_field = udp_read_be16(&payload[6]);
    if (checksum_field != 0U)
    {
        if (udp_checksum(src_ip, dst_ip, payload, udp_len) != 0U)
        {
            return;
        }
    }

    src_port = udp_read_be16(&payload[0]);
    dst_port = udp_read_be16(&payload[2]);
    data = payload + UDP_HDR_LEN;
    data_len = (uint16_t)(udp_len - UDP_HDR_LEN);

    klog(KLOG_INFO, "UDP", "Packet received");
    klog_uint(KLOG_INFO, "UDP", "Source port = ", src_port);
    klog_uint(KLOG_INFO, "UDP", "Destination port = ", dst_port);
    klog_uint(KLOG_INFO, "UDP", "Payload length = ", data_len);

    handler = udp_find_handler(dst_port);
    if (handler != NULL)
    {
        handler(src_ip, src_port, dst_ip, dst_port, data, data_len);
    }
}

static void udp_echo_handler(
    ipv4_addr_t src_ip,
    uint16_t src_port,
    ipv4_addr_t dst_ip,
    uint16_t dst_port,
    const uint8_t *data,
    uint16_t data_len
)
{
    (void)dst_ip;
    (void)dst_port;

    klog_uint(KLOG_INFO, "UDP", "Echo payload length = ", data_len);

    if (udp_send(src_ip, src_port, UDP_ECHO_PORT, data, data_len))
    {
        klog(KLOG_INFO, "UDP", "Echo reply sent");
    }
}

void udp_init(void)
{
    uint32_t i;

    for (i = 0; i < UDP_BIND_MAX; i++)
    {
        udp_binds[i].in_use = 0;
        udp_binds[i].port = 0;
        udp_binds[i].handler = NULL;
    }

    if (!udp_bind(UDP_ECHO_PORT, udp_echo_handler))
    {
        klog(KLOG_ERROR, "UDP", "Failed to bind echo port");
        return;
    }

    klog(KLOG_INFO, "UDP", "Echo service ready");
    klog_uint(KLOG_INFO, "UDP", "Port = ", UDP_ECHO_PORT);
}
