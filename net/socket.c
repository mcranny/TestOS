#include "socket.h"
#include "tcp.h"
#include "memory.h"

#define SOCKET_PENDING_MAX 4U

typedef enum { SOCKET_FREE, SOCKET_NEW, SOCKET_BOUND, SOCKET_LISTENING,
               SOCKET_CONNECTING, SOCKET_ESTABLISHED, SOCKET_PEER_CLOSED,
               SOCKET_FAILED } socket_state_t;
typedef struct {
    uint16_t generation;
    socket_state_t state;
    uint16_t port, remote_port;
    ipv4_addr_t remote_ip;
    uint8_t backlog, pending_count;
    int pending[SOCKET_PENDING_MAX];
    uint16_t rx_start, rx_end;
    uint8_t rx[SOCKET_RX_MAX];
} socket_t;
static socket_t sockets[SOCKET_MAX];

static int encode(uint32_t i) { return (int)((sockets[i].generation << 4) | (i + 1U)); }
static socket_t *get(int handle)
{
    uint32_t slot;
    if (handle <= 0) return NULL;
    slot = ((uint32_t)handle & 0x0fU);
    if (slot == 0U || slot > SOCKET_MAX) return NULL;
    slot--;
    if (sockets[slot].state == SOCKET_FREE || sockets[slot].generation != ((uint32_t)handle >> 4)) return NULL;
    return &sockets[slot];
}
static socket_t *find_listener(uint16_t port)
{
    uint32_t i;
    for (i = 0; i < SOCKET_MAX; i++) if (sockets[i].state == SOCKET_LISTENING && sockets[i].port == port) return &sockets[i];
    return NULL;
}
static socket_t *find_peer(ipv4_addr_t ip, uint16_t local, uint16_t remote)
{
    uint32_t i;
    for (i = 0; i < SOCKET_MAX; i++) if (sockets[i].state != SOCKET_FREE && sockets[i].remote_ip == ip && sockets[i].port == local && sockets[i].remote_port == remote) return &sockets[i];
    return NULL;
}
static int allocate(void)
{
    uint32_t i;
    for (i = 0; i < SOCKET_MAX; i++) if (sockets[i].state == SOCKET_FREE) {
        uint16_t next = (uint16_t)(sockets[i].generation + 1U);
        memset(&sockets[i], 0, sizeof(sockets[i])); sockets[i].generation = next ? next : 1U; sockets[i].state = SOCKET_NEW;
        return encode(i);
    }
    return SOCKET_ERROR;
}
static void on_established(ipv4_addr_t ip, uint16_t local, uint16_t remote)
{
    socket_t *listener = find_listener(local); socket_t *child = find_peer(ip, local, remote); int h;
    if (child && child->state == SOCKET_CONNECTING) { child->state = SOCKET_ESTABLISHED; return; }
    if (!listener || listener->pending_count >= listener->backlog) { (void)tcp_close(ip, remote, local); return; }
    h = allocate(); if (h < 0) { (void)tcp_close(ip, remote, local); return; }
    child = get(h); child->state = SOCKET_ESTABLISHED; child->port = local; child->remote_port = remote; child->remote_ip = ip;
    listener->pending[listener->pending_count++] = h;
}
static void on_data(ipv4_addr_t ip, uint16_t local, uint16_t remote, const uint8_t *data, uint16_t length)
{
    socket_t *s = find_peer(ip, local, remote); uint16_t room;
    if (!s || !data) return;
    room = (uint16_t)(SOCKET_RX_MAX - s->rx_end);
    if (length > room) { s->state = SOCKET_FAILED; return; }
    memcpy(&s->rx[s->rx_end], data, length); s->rx_end = (uint16_t)(s->rx_end + length);
}
static void on_closed(ipv4_addr_t ip, uint16_t local, uint16_t remote, int failed)
{
    socket_t *s = find_peer(ip, local, remote);
    if (s) s->state = failed ? SOCKET_FAILED : SOCKET_PEER_CLOSED;
}
static const tcp_app_callbacks_t callbacks = { on_established, on_data, on_closed };

void socket_init(void) { memset(sockets, 0, sizeof(sockets)); tcp_set_app_callbacks(&callbacks); }
int socket_create(void) { return allocate(); }
int socket_bind(int h, uint16_t port)
{
    socket_t *s = get(h); uint32_t i;
    if (!s || s->state != SOCKET_NEW || !port) return SOCKET_ERROR;
    for (i = 0; i < SOCKET_MAX; i++) if (sockets[i].state != SOCKET_FREE && sockets[i].port == port) return SOCKET_ERROR;
    s->port = port; s->state = SOCKET_BOUND; return 1;
}
int socket_listen(int h, uint8_t backlog)
{
    socket_t *s = get(h);
    if (!s || s->state != SOCKET_BOUND || !backlog) return SOCKET_ERROR;
    if (backlog > SOCKET_PENDING_MAX) backlog = SOCKET_PENDING_MAX;
    if (!tcp_register_listener(s->port, &callbacks)) return SOCKET_ERROR;
    s->backlog = backlog; s->state = SOCKET_LISTENING; return 1;
}
int socket_accept(int h)
{
    socket_t *s = get(h); int child; uint8_t i;
    if (!s || s->state != SOCKET_LISTENING) return SOCKET_ERROR;
    if (!s->pending_count) return SOCKET_WOULD_BLOCK;
    child = s->pending[0]; for (i = 1; i < s->pending_count; i++) s->pending[i - 1] = s->pending[i]; s->pending_count--; return child;
}
int socket_connect(int h, ipv4_addr_t ip, uint16_t port, uint16_t local)
{
    socket_t *s = get(h);
    if (!s || s->state != SOCKET_NEW || !ip || !port || !local || !tcp_connect(ip, port, local)) return SOCKET_ERROR;
    s->state = SOCKET_CONNECTING; s->remote_ip = ip; s->remote_port = port; s->port = local; return 1;
}
int socket_send(int h, const void *data, uint16_t length)
{
    socket_t *s = get(h);
    if (!s || s->state != SOCKET_ESTABLISHED || (!data && length)) return SOCKET_ERROR;
    if (!length) return 0;
    if (length > TCP_DATA_MAX) length = TCP_DATA_MAX;
    return tcp_send(s->remote_ip, s->remote_port, s->port, data, length) ? (int)length : SOCKET_WOULD_BLOCK;
}
int socket_recv(int h, void *data, uint16_t cap)
{
    socket_t *s = get(h); uint16_t n;
    if (!s || (!data && cap)) return SOCKET_ERROR;
    n = (uint16_t)(s->rx_end - s->rx_start);
    if (n) { if (n > cap) n = cap; memcpy(data, &s->rx[s->rx_start], n); s->rx_start = (uint16_t)(s->rx_start + n); if (s->rx_start == s->rx_end) s->rx_start = s->rx_end = 0; return n; }
    return s->state == SOCKET_PEER_CLOSED ? SOCKET_EOF : (s->state == SOCKET_FAILED ? SOCKET_ERROR : SOCKET_WOULD_BLOCK);
}
int socket_close(int h)
{
    socket_t *s = get(h); uint8_t i;
    if (!s) return SOCKET_ERROR;
    if (s->state == SOCKET_LISTENING) (void)tcp_unregister_listener(s->port);
    if (s->state == SOCKET_ESTABLISHED && !tcp_close(s->remote_ip, s->remote_port, s->port)) return SOCKET_WOULD_BLOCK;
    for (i = 0; i < s->pending_count; i++) (void)socket_close(s->pending[i]);
    s->state = SOCKET_FREE; return 1;
}
