#ifndef TCP_H
#define TCP_H

#include "types.h"
#include "ipv4.h"

#define TCP_HDR_MIN_LEN 20U
#define TCP_ECHO_PORT 12346U

void tcp_init(void);
void tcp_input(const uint8_t *payload, uint16_t length,
               ipv4_addr_t src_ip, ipv4_addr_t dst_ip);
void tcp_timer_tick(void);

/* A small asynchronous active-open interface for kernel callers. */
int tcp_connect(ipv4_addr_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_send(ipv4_addr_t dst_ip, uint16_t dst_port, uint16_t src_port,
             const void *data, uint16_t length);
int tcp_close(ipv4_addr_t dst_ip, uint16_t dst_port, uint16_t src_port);

/* Focused freestanding checks used only by the headless regression build. */
int tcp_selftest(void);
void tcp_test_drop_next_segment(void);
void tcp_test_drop_next_payload(void);

#endif
