#ifndef TCP_H
#define TCP_H

#include "types.h"
#include "ipv4.h"

#define TCP_HDR_MIN_LEN 20U
#define TCP_ECHO_PORT 12346U
/* Typical MSS (ETH 1500 - IPv4 20 - TCP 20); receive path matches advertised window. */
#define TCP_DATA_MAX 1460U

/* Transport callbacks use tuple values, never TCP control-block pointers. */
typedef struct {
    void (*established)(ipv4_addr_t address, uint16_t local_port, uint16_t remote_port);
    /* Return non-zero if data was accepted; 0 refuses (do not ACK / advance rcv_nxt). */
    int (*data)(ipv4_addr_t address, uint16_t local_port, uint16_t remote_port,
                const uint8_t *data, uint16_t length);
    void (*closed)(ipv4_addr_t address, uint16_t local_port, uint16_t remote_port, int failed);
} tcp_app_callbacks_t;

void tcp_init(void);
void tcp_input(const uint8_t *payload, uint16_t length,
               ipv4_addr_t src_ip, ipv4_addr_t dst_ip);
void tcp_timer_tick(void);

/* A small asynchronous active-open interface for kernel callers. */
int tcp_connect(ipv4_addr_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_send(ipv4_addr_t dst_ip, uint16_t dst_port, uint16_t src_port,
             const void *data, uint16_t length);
int tcp_close(ipv4_addr_t dst_ip, uint16_t dst_port, uint16_t src_port);
int tcp_register_listener(uint16_t port, const tcp_app_callbacks_t *callbacks);
int tcp_unregister_listener(uint16_t port);
void tcp_set_app_callbacks(const tcp_app_callbacks_t *callbacks);

typedef struct
{
    int used;
    int state;
    ipv4_addr_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
} tcp_conn_info_t;

const char *tcp_state_name(int state);
uint32_t tcp_conn_count(void);
int tcp_conn_get(uint32_t index, tcp_conn_info_t *out);

/* Focused freestanding checks used only by the headless regression build. */
int tcp_selftest(void);
void tcp_test_drop_next_segment(void);
void tcp_test_drop_next_payload(void);

#endif
