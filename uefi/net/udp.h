#ifndef UDP_H
#define UDP_H

#include "types.h"
#include "ipv4.h"

#define UDP_HDR_LEN    8U
#define UDP_ECHO_PORT  12345U
#define UDP_BIND_MAX   8U

typedef struct
{
    uint16_t source_port;
    uint16_t destination_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef void (*udp_handler_t)(
    ipv4_addr_t src_ip,
    uint16_t src_port,
    ipv4_addr_t dst_ip,
    uint16_t dst_port,
    const uint8_t *data,
    uint16_t data_len
);

void udp_init(void);
int udp_bind(uint16_t port, udp_handler_t handler);
int udp_send(
    ipv4_addr_t dst_ip,
    uint16_t dst_port,
    uint16_t src_port,
    const void *payload,
    uint16_t payload_len
);
void udp_input(
    const uint8_t *payload,
    uint16_t length,
    ipv4_addr_t src_ip,
    ipv4_addr_t dst_ip
);

#endif
