#include "icmp.h"
#include "checksum.h"
#include "ethernet.h"
#include "log.h"
#include "memory.h"

static const char icmp_echo_payload[] = "TestOS Ping";

typedef struct
{
    int armed;
    int done;
    ipv4_addr_t src_ip;
    uint16_t id;
    uint16_t seq;
} icmp_echo_wait_t;

static icmp_echo_wait_t icmp_echo_wait;

static uint16_t icmp_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void icmp_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)((value >> 8) & 0xFFU);
    p[1] = (uint8_t)(value & 0xFFU);
}

void icmp_arm_echo_wait(ipv4_addr_t src_ip, uint16_t id, uint16_t seq)
{
    icmp_echo_wait.armed = 1;
    icmp_echo_wait.done = 0;
    icmp_echo_wait.src_ip = src_ip;
    icmp_echo_wait.id = id;
    icmp_echo_wait.seq = seq;
}

int icmp_echo_wait_done(void)
{
    return icmp_echo_wait.done;
}

static int icmp_send_echo_reply(
    ipv4_addr_t dst_ip,
    const uint8_t *request,
    uint16_t length
)
{
    uint8_t reply[ETH_MAX_PAYLOAD];
    uint16_t checksum;

    if (request == NULL || length < ICMP_HDR_LEN || length > ETH_MAX_PAYLOAD)
    {
        return 0;
    }

    memcpy(reply, request, length);
    reply[0] = ICMP_TYPE_ECHO_REPLY;
    reply[1] = 0;
    icmp_write_be16(&reply[2], 0);

    checksum = internet_checksum(reply, length);
    icmp_write_be16(&reply[2], checksum);

    klog(KLOG_INFO, "ICMP", "Echo Reply sent");
    return ipv4_send(dst_ip, IPV4_PROTO_ICMP, reply, length);
}

int icmp_send_echo_request(ipv4_addr_t dst, uint16_t id, uint16_t seq)
{
    uint8_t packet[ICMP_HDR_LEN + sizeof(icmp_echo_payload) - 1U];
    uint16_t payload_len = (uint16_t)(sizeof(icmp_echo_payload) - 1U);
    uint16_t total = (uint16_t)(ICMP_HDR_LEN + payload_len);
    uint16_t checksum;

    if (dst == 0U)
    {
        return 0;
    }

    memset(packet, 0, sizeof(packet));
    packet[0] = ICMP_TYPE_ECHO_REQUEST;
    packet[1] = 0;
    icmp_write_be16(&packet[2], 0);
    icmp_write_be16(&packet[4], id);
    icmp_write_be16(&packet[6], seq);
    memcpy(&packet[ICMP_HDR_LEN], icmp_echo_payload, payload_len);

    checksum = internet_checksum(packet, total);
    icmp_write_be16(&packet[2], checksum);

    klog(KLOG_INFO, "ICMP", "Echo Request sent");
    klog_uint(KLOG_INFO, "ICMP", "Identifier = ", id);
    klog_uint(KLOG_INFO, "ICMP", "Sequence = ", seq);

    return ipv4_send(dst, IPV4_PROTO_ICMP, packet, total);
}

void icmp_input(
    const uint8_t *payload,
    uint16_t length,
    ipv4_addr_t src_ip,
    ipv4_addr_t dst_ip
)
{
    uint8_t type;
    uint8_t code;
    uint16_t id;
    uint16_t seq;

    (void)dst_ip;

    if (payload == NULL || length < ICMP_HDR_LEN)
    {
        return;
    }

    if (internet_checksum(payload, length) != 0U)
    {
        return;
    }

    type = payload[0];
    code = payload[1];
    id = icmp_read_be16(&payload[4]);
    seq = icmp_read_be16(&payload[6]);

    if (type == ICMP_TYPE_ECHO_REQUEST && code == 0U)
    {
        (void)icmp_send_echo_reply(src_ip, payload, length);
        return;
    }

    if (type == ICMP_TYPE_ECHO_REPLY)
    {
        klog(KLOG_INFO, "ICMP", "Echo Reply received");
        klog_uint(KLOG_INFO, "ICMP", "Identifier = ", id);
        klog_uint(KLOG_INFO, "ICMP", "Sequence = ", seq);

        if (icmp_echo_wait.armed &&
            src_ip == icmp_echo_wait.src_ip &&
            id == icmp_echo_wait.id &&
            seq == icmp_echo_wait.seq)
        {
            icmp_echo_wait.done = 1;
            icmp_echo_wait.armed = 0;
        }
    }
}
