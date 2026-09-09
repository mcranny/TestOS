#include "dns.h"
#include "udp.h"
#include "ipv4.h"
#include "netif.h"
#include "ethernet.h"
#include "platform.h"
#include "lib/string.h"

#define DNS_PORT 53U
#define DNS_CLIENT_PORT 53000U
#define DNS_CACHE_MAX 8U
#define DNS_NAME_MAX 64U
#define DNS_PACKET_MAX 512U

typedef struct
{
    char name[DNS_NAME_MAX];
    ipv4_addr_t ip;
    int valid;
} dns_cache_entry_t;

static dns_cache_entry_t dns_cache[DNS_CACHE_MAX];
static volatile int dns_waiting;
static volatile ipv4_addr_t dns_result_ip;
static volatile uint16_t dns_query_id;
static char dns_query_name[DNS_NAME_MAX];

static int dns_name_eq(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; ; i++) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        if (ca == '\0') return 1;
    }
}

static void dns_cache_insert(const char *name, ipv4_addr_t ip)
{
    uint32_t i;
    int free_slot = -1;

    if (name == NULL || ip == 0U) {
        return;
    }
    for (i = 0; i < DNS_CACHE_MAX; i++) {
        if (dns_cache[i].valid && dns_name_eq(dns_cache[i].name, name)) {
            dns_cache[i].ip = ip;
            return;
        }
        if (!dns_cache[i].valid && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (free_slot < 0) {
        free_slot = 0;
    }
    {
        uint32_t n = 0;
        while (name[n] != '\0' && n + 1U < DNS_NAME_MAX) {
            dns_cache[free_slot].name[n] = name[n];
            n++;
        }
        dns_cache[free_slot].name[n] = '\0';
    }
    dns_cache[free_slot].ip = ip;
    dns_cache[free_slot].valid = 1;
}

static int dns_cache_lookup(const char *name, ipv4_addr_t *out)
{
    uint32_t i;
    for (i = 0; i < DNS_CACHE_MAX; i++) {
        if (dns_cache[i].valid && dns_name_eq(dns_cache[i].name, name)) {
            if (out != NULL) {
                *out = dns_cache[i].ip;
            }
            return 1;
        }
    }
    return 0;
}

static int dns_encode_name(uint8_t *out, uint16_t cap, const char *hostname, uint16_t *used)
{
    uint16_t pos = 0;
    uint16_t label_start;
    uint16_t i = 0;

    if (out == NULL || hostname == NULL || used == NULL) {
        return 0;
    }
    while (hostname[i] != '\0') {
        uint16_t label_len = 0;
        label_start = pos;
        if (pos + 1U >= cap) {
            return 0;
        }
        pos++;
        while (hostname[i] != '\0' && hostname[i] != '.') {
            if (pos >= cap || label_len >= 63U) {
                return 0;
            }
            out[pos++] = (uint8_t)hostname[i++];
            label_len++;
        }
        out[label_start] = (uint8_t)label_len;
        if (hostname[i] == '.') {
            i++;
        }
    }
    if (pos >= cap) {
        return 0;
    }
    out[pos++] = 0;
    *used = pos;
    return 1;
}

static uint16_t dns_skip_name(const uint8_t *pkt, uint16_t len, uint16_t offset)
{
    while (offset < len) {
        uint8_t lab = pkt[offset];
        if (lab == 0) {
            return (uint16_t)(offset + 1U);
        }
        if ((lab & 0xC0U) == 0xC0U) {
            return (uint16_t)(offset + 2U);
        }
        offset = (uint16_t)(offset + 1U + lab);
    }
    return len;
}

static void dns_on_udp(
    ipv4_addr_t src_ip,
    uint16_t src_port,
    ipv4_addr_t dst_ip,
    uint16_t dst_port,
    const uint8_t *data,
    uint16_t data_len
)
{
    uint16_t id;
    uint16_t flags;
    uint16_t ancount;
    uint16_t offset;
    uint16_t i;

    (void)src_ip;
    (void)dst_ip;
    (void)dst_port;

    if (src_port != DNS_PORT || data == NULL || data_len < 12U || !dns_waiting) {
        return;
    }

    id = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    if (id != dns_query_id) {
        return;
    }
    flags = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
    if ((flags & 0x8000U) == 0) {
        return;
    }
    ancount = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
    offset = dns_skip_name(data, data_len, 12);
    if (offset + 4U > data_len) {
        return;
    }
    offset = (uint16_t)(offset + 4U); /* QTYPE QCLASS */

    for (i = 0; i < ancount && offset + 12U <= data_len; i++) {
        uint16_t type;
        uint16_t rdlen;

        offset = dns_skip_name(data, data_len, offset);
        if (offset + 10U > data_len) {
            return;
        }
        type = (uint16_t)(((uint16_t)data[offset] << 8) | data[offset + 1U]);
        rdlen = (uint16_t)(((uint16_t)data[offset + 8U] << 8) | data[offset + 9U]);
        offset = (uint16_t)(offset + 10U);
        if (offset + rdlen > data_len) {
            return;
        }
        if (type == 1U && rdlen == 4U) {
            dns_result_ip = ipv4_addr_from_bytes(&data[offset]);
            dns_waiting = 0;
            dns_cache_insert(dns_query_name, dns_result_ip);
            return;
        }
        offset = (uint16_t)(offset + rdlen);
    }
}

static int dns_send_query(const char *hostname, ipv4_addr_t server)
{
    uint8_t packet[DNS_PACKET_MAX];
    uint16_t name_len = 0;
    uint16_t total;
    static uint16_t next_id = 1U;

    memset(packet, 0, sizeof(packet));
    dns_query_id = next_id++;
    if (next_id == 0U) {
        next_id = 1U;
    }
    packet[0] = (uint8_t)((dns_query_id >> 8) & 0xFFU);
    packet[1] = (uint8_t)(dns_query_id & 0xFFU);
    packet[2] = 0x01; /* RD */
    packet[3] = 0x00;
    packet[4] = 0x00;
    packet[5] = 0x01; /* QDCOUNT = 1 */

    if (!dns_encode_name(&packet[12], (uint16_t)(sizeof(packet) - 16U), hostname, &name_len)) {
        return 0;
    }
    total = (uint16_t)(12U + name_len + 4U);
    packet[12U + name_len] = 0x00;
    packet[12U + name_len + 1U] = 0x01; /* A */
    packet[12U + name_len + 2U] = 0x00;
    packet[12U + name_len + 3U] = 0x01; /* IN */

    return udp_send(server, DNS_PORT, DNS_CLIENT_PORT, packet, total);
}

void dns_init(void)
{
    uint32_t i;
    for (i = 0; i < DNS_CACHE_MAX; i++) {
        dns_cache[i].valid = 0;
    }
    dns_waiting = 0;
    dns_result_ip = 0;
    dns_query_id = 0;
    dns_query_name[0] = '\0';
    (void)udp_bind(DNS_CLIENT_PORT, dns_on_udp);
}

int dns_resolve_timeout(const char *hostname, ipv4_addr_t *out, uint32_t timeout_ticks)
{
    ipv4_addr_t server;
    uint32_t start;
    uint32_t n;
    int is_ip = 1;
    ipv4_addr_t parsed = 0;

    if (hostname == NULL || out == NULL || hostname[0] == '\0') {
        return 0;
    }

    /* Numeric IPv4 short-circuit. */
    {
        uint32_t octets[4];
        uint32_t oi = 0;
        uint32_t val = 0;
        const char *p = hostname;
        int saw_digit = 0;
        while (*p != '\0') {
            if (*p >= '0' && *p <= '9') {
                val = val * 10U + (uint32_t)(*p - '0');
                if (val > 255U) {
                    is_ip = 0;
                    break;
                }
                saw_digit = 1;
                p++;
            } else if (*p == '.' && saw_digit && oi < 3U) {
                octets[oi++] = val;
                val = 0;
                saw_digit = 0;
                p++;
            } else {
                is_ip = 0;
                break;
            }
        }
        if (is_ip && saw_digit && oi == 3U) {
            octets[3] = val;
            parsed = IPV4_ADDR(octets[0], octets[1], octets[2], octets[3]);
            *out = parsed;
            return 1;
        }
    }

    if (dns_cache_lookup(hostname, out)) {
        return 1;
    }

    server = netif_get_dns(0);
    if (server == 0U) {
        server = IPV4_ADDR(10, 0, 2, 3);
    }

    n = 0;
    while (hostname[n] != '\0' && n + 1U < DNS_NAME_MAX) {
        dns_query_name[n] = hostname[n];
        n++;
    }
    dns_query_name[n] = '\0';

    dns_waiting = 1;
    dns_result_ip = 0;
    if (!dns_send_query(hostname, server)) {
        dns_waiting = 0;
        return 0;
    }

    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < timeout_ticks) {
        (void)ethernet_poll();
        if (!dns_waiting) {
            *out = dns_result_ip;
            return dns_result_ip != 0U;
        }
    }
    dns_waiting = 0;
    return 0;
}

int dns_resolve(const char *hostname, ipv4_addr_t *out)
{
    /* First try allows ARP resolution of the DNS server; retry covers late replies. */
    if (dns_resolve_timeout(hostname, out, 5U * TIMER_FREQUENCY)) {
        return 1;
    }
    return dns_resolve_timeout(hostname, out, 5U * TIMER_FREQUENCY);
}
