#ifndef IPV4_H
#define IPV4_H

#include "types.h"
#include "mac.h"

/* Host-order IPv4 address (a.b.c.d => a<<24 | b<<16 | c<<8 | d). */
typedef uint32_t ipv4_addr_t;

#define IPV4_ADDR(a, b, c, d) \
    ((((uint32_t)(a) & 0xFFU) << 24) | \
     (((uint32_t)(b) & 0xFFU) << 16) | \
     (((uint32_t)(c) & 0xFFU) << 8) | \
     ((uint32_t)(d) & 0xFFU))

#define IPV4_VERSION       4U
#define IPV4_IHL_MIN       5U
#define IPV4_HDR_MIN_LEN   20U
#define IPV4_PROTO_ICMP    1U
#define IPV4_PROTO_UDP     17U
#define IPV4_TTL_DEFAULT   64U

typedef struct
{
    uint8_t ver_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint8_t src_ip[4];
    uint8_t dst_ip[4];
} __attribute__((packed)) ipv4_header_t;

void ipv4_addr_to_bytes(ipv4_addr_t ip, uint8_t out[4]);
ipv4_addr_t ipv4_addr_from_bytes(const uint8_t in[4]);
void ipv4_addr_format(ipv4_addr_t ip, char *out /* >= 16 */);

void ipv4_input(const uint8_t *payload, uint16_t length, const mac_addr_t *src_mac);
int ipv4_send(
    ipv4_addr_t dst,
    uint8_t protocol,
    const void *payload,
    uint16_t payload_len
);

#endif
