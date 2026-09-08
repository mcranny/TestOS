#ifndef ETHERNET_H
#define ETHERNET_H

#include "types.h"
#include "mac.h"

#define ETHERTYPE_ARP     0x0806U
#define ETHERTYPE_IPV4    0x0800U
#define ETH_HDR_LEN       14U
#define ETH_MIN_FRAME     60U
#define ETH_MAX_FRAME     1514U
#define ETH_MAX_PAYLOAD   1500U

typedef struct
{
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype;
} __attribute__((packed)) ethernet_header_t;

int ethernet_send(
    const mac_addr_t *dst,
    uint16_t ethertype,
    const void *payload,
    uint16_t payload_len
);

void ethernet_input(const uint8_t *frame, uint16_t length);
int ethernet_poll(void);
void net_bootstrap(void);

#endif
