#ifndef ICMP_H
#define ICMP_H

#include "types.h"
#include "ipv4.h"

#define ICMP_TYPE_ECHO_REPLY   0U
#define ICMP_TYPE_ECHO_REQUEST 8U
#define ICMP_HDR_LEN           8U

typedef struct
{
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_echo_header_t;

void icmp_input(
    const uint8_t *payload,
    uint16_t length,
    ipv4_addr_t src_ip,
    ipv4_addr_t dst_ip
);

int icmp_send_echo_request(ipv4_addr_t dst, uint16_t id, uint16_t seq);
void icmp_arm_echo_wait(ipv4_addr_t src_ip, uint16_t id, uint16_t seq);
int icmp_echo_wait_done(void);

#endif
