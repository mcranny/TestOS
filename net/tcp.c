#include "tcp.h"
#include "arp.h"
#include "checksum.h"
#include "ethernet.h"
#include "ipv4.h"
#include "log.h"
#include "memory.h"
#include "timer.h"

#define TCP_CONN_MAX 8U
#define TCP_DATA_MAX 1024U
#define TCP_WINDOW TCP_DATA_MAX
#define TCP_RETRY_TICKS (TIMER_FREQUENCY * 2U)
#define TCP_RETRY_MAX 3U

#define TCP_FIN 0x01U
#define TCP_SYN 0x02U
#define TCP_RST 0x04U
#define TCP_PSH 0x08U
#define TCP_ACK 0x10U

typedef enum {
    TCP_CLOSED, TCP_LISTEN, TCP_SYN_SENT, TCP_SYN_RECEIVED,
    TCP_ESTABLISHED, TCP_FIN_WAIT_1, TCP_FIN_WAIT_2, TCP_LAST_ACK
} tcp_state_t;

typedef struct {
    int used;
    tcp_state_t state;
    ipv4_addr_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t last_tx_tick;
    uint8_t retries;
    uint8_t unacked_flags;
    uint16_t unacked_len;
    uint8_t unacked_data[TCP_DATA_MAX];
} tcp_conn_t;

static tcp_conn_t tcp_conns[TCP_CONN_MAX];
static uint32_t tcp_iss = 0x10000000U;
#ifdef TESTOS_TCP_TEST_HOOKS
static uint32_t tcp_test_drop_count;
static uint32_t tcp_test_drop_payload_count;
#endif

static uint16_t be16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static int seq_lt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static int seq_leq(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

static uint16_t tcp_checksum(ipv4_addr_t src, ipv4_addr_t dst, const uint8_t *seg, uint16_t len)
{
    uint8_t buf[12U + TCP_HDR_MIN_LEN + TCP_DATA_MAX];
    if (seg == NULL || len < TCP_HDR_MIN_LEN || len > TCP_HDR_MIN_LEN + TCP_DATA_MAX) return 0xffffU;
    ipv4_addr_to_bytes(src, &buf[0]);
    ipv4_addr_to_bytes(dst, &buf[4]);
    buf[8] = 0; buf[9] = 6; put16(&buf[10], len);
    memcpy(&buf[12], seg, len);
    return internet_checksum(buf, (uint16_t)(12U + len));
}

static tcp_conn_t *tcp_find(ipv4_addr_t ip, uint16_t local, uint16_t remote)
{
    uint32_t i;
    for (i = 0; i < TCP_CONN_MAX; i++)
        if (tcp_conns[i].used && tcp_conns[i].remote_ip == ip &&
            tcp_conns[i].local_port == local && tcp_conns[i].remote_port == remote) return &tcp_conns[i];
    return NULL;
}

static tcp_conn_t *tcp_alloc(void)
{
    uint32_t i;
    for (i = 0; i < TCP_CONN_MAX; i++) if (!tcp_conns[i].used) { memset(&tcp_conns[i], 0, sizeof(tcp_conns[i])); tcp_conns[i].used = 1; return &tcp_conns[i]; }
    return NULL;
}

static int tcp_emit(tcp_conn_t *c, uint8_t flags, const uint8_t *data, uint16_t len, int remember)
{
    uint8_t seg[TCP_HDR_MIN_LEN + TCP_DATA_MAX];
    uint16_t total = (uint16_t)(TCP_HDR_MIN_LEN + len);
    uint16_t sum;
    if (c == NULL || len > TCP_DATA_MAX || (len != 0U && data == NULL)) return 0;
    memset(seg, 0, TCP_HDR_MIN_LEN);
    put16(&seg[0], c->local_port); put16(&seg[2], c->remote_port);
    put32(&seg[4], c->snd_nxt); put32(&seg[8], c->rcv_nxt);
    seg[12] = (uint8_t)(5U << 4); seg[13] = flags;
    put16(&seg[14], TCP_WINDOW); put16(&seg[16], 0);
    if (len) memcpy(&seg[TCP_HDR_MIN_LEN], data, len);
    sum = tcp_checksum(arp_get_local_ip(), c->remote_ip, seg, total);
    put16(&seg[16], sum);
#ifdef TESTOS_TCP_TEST_HOOKS
    if (tcp_test_drop_count != 0U)
    {
        tcp_test_drop_count--;
        klog(KLOG_INFO, "TCP", "Test hook dropped segment");
    }
    else
#endif
    if (!ipv4_send(c->remote_ip, IPV4_PROTO_TCP, seg, total)) return 0;
    if (remember) {
        c->unacked_flags = flags; c->unacked_len = len;
        if (len) memcpy(c->unacked_data, data, len);
        c->last_tx_tick = timer_get_ticks(); c->retries = 0;
    }
    if (flags & TCP_SYN) c->snd_nxt++;
    if (flags & TCP_FIN) c->snd_nxt++;
    c->snd_nxt += len;
    return 1;
}

static void tcp_ack(tcp_conn_t *c) { (void)tcp_emit(c, TCP_ACK, NULL, 0, 0); }

static void tcp_reset(
    ipv4_addr_t dst,
    uint16_t local,
    uint16_t remote,
    uint32_t seq,
    uint32_t ack,
    uint16_t data_len,
    uint8_t flags
)
{
    tcp_conn_t temp;
    memset(&temp, 0, sizeof(temp)); temp.remote_ip = dst; temp.local_port = local; temp.remote_port = remote;
    if (flags & TCP_ACK) { temp.snd_nxt = ack; (void)tcp_emit(&temp, TCP_RST, NULL, 0, 0); }
    else
    {
        temp.rcv_nxt = seq + data_len;
        if (flags & TCP_SYN) temp.rcv_nxt++;
        if (flags & TCP_FIN) temp.rcv_nxt++;
        (void)tcp_emit(&temp, (uint8_t)(TCP_RST | TCP_ACK), NULL, 0, 0);
    }
}

static void tcp_retransmit(tcp_conn_t *c)
{
    uint32_t saved = c->snd_nxt;
    c->snd_nxt = c->snd_una;
    (void)tcp_emit(c, c->unacked_flags, c->unacked_data, c->unacked_len, 0);
    c->snd_nxt = saved;
    c->last_tx_tick = timer_get_ticks(); c->retries++;
    klog(KLOG_WARN, "TCP", "Retransmitted segment");
}

void tcp_init(void)
{
    memset(tcp_conns, 0, sizeof(tcp_conns));
#ifdef TESTOS_TCP_TEST_HOOKS
    tcp_test_drop_count = 0;
    tcp_test_drop_payload_count = 0;
#endif
    klog(KLOG_INFO, "TCP", "Echo service ready");
    klog_uint(KLOG_INFO, "TCP", "Port = ", TCP_ECHO_PORT);
}

void tcp_test_drop_next_segment(void)
{
#ifdef TESTOS_TCP_TEST_HOOKS
    tcp_test_drop_count = 1U;
#endif
}

void tcp_test_drop_next_payload(void)
{
#ifdef TESTOS_TCP_TEST_HOOKS
    tcp_test_drop_payload_count = 1U;
#endif
}

int tcp_selftest(void)
{
    uint8_t truncated[TCP_HDR_MIN_LEN];
    uint32_t before;
    uint32_t after;
    uint32_t i;

    if (internet_checksum("\x01\x02\x03", 3U) != 0xFBFDU ||
        !seq_lt(0xFFFFFFF0U, 0x00000010U) ||
        seq_lt(0x00000010U, 0xFFFFFFF0U) ||
        !seq_leq(0x00000010U, 0x00000010U))
    {
        return 0;
    }

    before = 0;
    for (i = 0; i < TCP_CONN_MAX; i++) before += (uint32_t)tcp_conns[i].used;

    memset(truncated, 0, sizeof(truncated));
    tcp_input(truncated, (uint16_t)(TCP_HDR_MIN_LEN - 1U),
              ARP_GATEWAY_IP_DEFAULT, arp_get_local_ip());
    truncated[12] = (uint8_t)(5U << 4);
    truncated[13] = TCP_SYN;
    tcp_input(truncated, TCP_HDR_MIN_LEN, ARP_GATEWAY_IP_DEFAULT,
              arp_get_local_ip());
    truncated[12] = (uint8_t)(6U << 4);
    tcp_input(truncated, TCP_HDR_MIN_LEN, ARP_GATEWAY_IP_DEFAULT,
              arp_get_local_ip());

    after = 0;
    for (i = 0; i < TCP_CONN_MAX; i++) after += (uint32_t)tcp_conns[i].used;
    return before == after;
}

int tcp_connect(ipv4_addr_t ip, uint16_t remote, uint16_t local)
{
    tcp_conn_t *c;
    if (!ip || !remote || !local || tcp_find(ip, local, remote)) return 0;
    c = tcp_alloc(); if (!c) return 0;
    c->state = TCP_SYN_SENT; c->remote_ip = ip; c->local_port = local; c->remote_port = remote;
    c->snd_una = tcp_iss; c->snd_nxt = tcp_iss; tcp_iss += 0x10000U;
    if (!tcp_emit(c, TCP_SYN, NULL, 0, 1))
    {
        c->used = 0;
        return 0;
    }
    return 1;
}

int tcp_send(ipv4_addr_t ip, uint16_t remote, uint16_t local, const void *data, uint16_t len)
{
    tcp_conn_t *c = tcp_find(ip, local, remote);
    if (!c || c->state != TCP_ESTABLISHED || len > TCP_DATA_MAX || (len && !data) || c->snd_una != c->snd_nxt) return 0;
    return tcp_emit(c, (uint8_t)(TCP_ACK | TCP_PSH), data, len, 1);
}

int tcp_close(ipv4_addr_t ip, uint16_t remote, uint16_t local)
{
    tcp_conn_t *c = tcp_find(ip, local, remote);
    if (!c || c->state != TCP_ESTABLISHED || c->snd_una != c->snd_nxt) return 0;
    c->state = TCP_FIN_WAIT_1;
    if (!tcp_emit(c, (uint8_t)(TCP_FIN | TCP_ACK), NULL, 0, 1))
    {
        c->state = TCP_ESTABLISHED;
        return 0;
    }
    return 1;
}

void tcp_input(const uint8_t *p, uint16_t length, ipv4_addr_t src, ipv4_addr_t dst)
{
    uint16_t hdr, src_port, dst_port, data_len;
    uint32_t seq, ack;
    uint8_t flags;
    tcp_conn_t *c;
    (void)dst;
    if (!p || length < TCP_HDR_MIN_LEN) return;
    hdr = (uint16_t)((p[12] >> 4) * 4U);
    if (hdr < TCP_HDR_MIN_LEN || hdr > length || tcp_checksum(src, arp_get_local_ip(), p, length) != 0U) return;
    src_port = be16(&p[0]); dst_port = be16(&p[2]); seq = be32(&p[4]); ack = be32(&p[8]); flags = p[13]; data_len = (uint16_t)(length - hdr);
    if (data_len > TCP_DATA_MAX) return;
    c = tcp_find(src, dst_port, src_port);
    if (!c) {
        if (dst_port != TCP_ECHO_PORT || !(flags & TCP_SYN) || (flags & TCP_ACK)) { if (!(flags & TCP_RST)) tcp_reset(src, dst_port, src_port, seq, ack, data_len, flags); return; }
        c = tcp_alloc(); if (!c) { tcp_reset(src, dst_port, src_port, seq, ack, data_len, flags); return; }
        c->state = TCP_SYN_RECEIVED; c->remote_ip = src; c->local_port = dst_port; c->remote_port = src_port;
        c->rcv_nxt = seq + 1U; c->snd_una = tcp_iss; c->snd_nxt = tcp_iss; tcp_iss += 0x10000U;
        if (!tcp_emit(c, (uint8_t)(TCP_SYN | TCP_ACK), NULL, 0, 1))
        {
            c->used = 0;
            return;
        }
        klog(KLOG_INFO, "TCP", "SYN received"); return;
    }
    if (flags & TCP_RST) { c->used = 0; klog(KLOG_WARN, "TCP", "Connection reset"); return; }
    if ((flags & TCP_ACK) && !seq_lt(ack, c->snd_una) && seq_leq(ack, c->snd_nxt)) {
        c->snd_una = ack;
        if (c->snd_una == c->snd_nxt) { c->unacked_flags = 0; c->unacked_len = 0; c->retries = 0; }
    }
    if (c->state == TCP_SYN_SENT && (flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK) && ack == c->snd_nxt) {
        c->rcv_nxt = seq + 1U; c->state = TCP_ESTABLISHED; tcp_ack(c); klog(KLOG_INFO, "TCP", "Active open established"); return;
    }
    if (c->state == TCP_SYN_RECEIVED && (flags & TCP_ACK) && ack == c->snd_nxt) { c->state = TCP_ESTABLISHED; klog(KLOG_INFO, "TCP", "Passive open established"); return; }
    if (c->state == TCP_FIN_WAIT_1 && c->snd_una == c->snd_nxt) c->state = TCP_FIN_WAIT_2;
    if (c->state == TCP_LAST_ACK && c->snd_una == c->snd_nxt) { c->used = 0; klog(KLOG_INFO, "TCP", "Connection closed"); return; }
    if (c->state != TCP_ESTABLISHED && c->state != TCP_FIN_WAIT_2) return;
    if (data_len) {
        if (seq != c->rcv_nxt || data_len > TCP_WINDOW) { tcp_ack(c); return; }
        c->rcv_nxt += data_len; tcp_ack(c);
        if (c->state == TCP_ESTABLISHED && c->local_port == TCP_ECHO_PORT)
        {
#ifdef TESTOS_TCP_TEST_HOOKS
            if (tcp_test_drop_payload_count != 0U)
            {
                tcp_test_drop_payload_count--;
                tcp_test_drop_next_segment();
            }
#endif
            (void)tcp_send(src, src_port, dst_port, &p[hdr], data_len);
        }
    }
    if (flags & TCP_FIN) {
        if (seq + data_len != c->rcv_nxt) { tcp_ack(c); return; }
        c->rcv_nxt++; tcp_ack(c);
        if (c->state == TCP_ESTABLISHED) { c->state = TCP_LAST_ACK; (void)tcp_emit(c, (uint8_t)(TCP_FIN | TCP_ACK), NULL, 0, 1); }
        else if (c->state == TCP_FIN_WAIT_2) c->used = 0;
    }
}

void tcp_timer_tick(void)
{
    uint32_t i, now = timer_get_ticks();
    for (i = 0; i < TCP_CONN_MAX; i++) if (tcp_conns[i].used && tcp_conns[i].unacked_flags &&
        (uint32_t)(now - tcp_conns[i].last_tx_tick) >= TCP_RETRY_TICKS) {
        if (tcp_conns[i].retries >= TCP_RETRY_MAX) { tcp_conns[i].used = 0; klog(KLOG_WARN, "TCP", "Connection timed out"); }
        else tcp_retransmit(&tcp_conns[i]);
    }
}
