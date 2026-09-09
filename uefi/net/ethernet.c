#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "socket.h"
#include "http.h"
#include "netif.h"
#include "route.h"
#include "dhcp.h"
#include "dns.h"
#include "e1000.h"
#include "platform.h"
#include "lib/string.h"

static uint16_t eth_read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void eth_write_be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)((value >> 8) & 0xFFU);
    p[1] = (uint8_t)(value & 0xFFU);
}

int ethernet_send(
    const mac_addr_t *dst,
    uint16_t ethertype,
    const void *payload,
    uint16_t payload_len
)
{
    uint8_t frame[ETH_MAX_FRAME];
    const mac_addr_t *local;
    netif_t *nif;
    uint16_t total;
    uint16_t i;

    if (dst == NULL || (payload == NULL && payload_len != 0) ||
        payload_len > ETH_MAX_PAYLOAD) {
        return 0;
    }

    local = netif_get_mac();
    nif = netif_get_primary();
    if (local == NULL) {
        return 0;
    }

    for (i = 0; i < 6U; i++) {
        frame[i] = dst->bytes[i];
        frame[6U + i] = local->bytes[i];
    }

    eth_write_be16(&frame[12], ethertype);

    if (payload_len > 0U) {
        memcpy(&frame[ETH_HDR_LEN], payload, payload_len);
    }

    total = (uint16_t)(ETH_HDR_LEN + payload_len);
    if (total < ETH_MIN_FRAME) {
        memset(&frame[total], 0, (size_t)(ETH_MIN_FRAME - total));
        total = ETH_MIN_FRAME;
    }

    if (nif != NULL) {
        return netif_transmit(nif, frame, total);
    }
    return e1000_transmit(frame, total);
}

void ethernet_input(const uint8_t *frame, uint16_t length)
{
    mac_addr_t dst;
    mac_addr_t src;
    const mac_addr_t *local;
    uint16_t ethertype;
    const uint8_t *payload;
    uint16_t payload_len;
    uint32_t i;

    if (frame == NULL || length < ETH_HDR_LEN) {
        return;
    }

    for (i = 0; i < 6U; i++) {
        dst.bytes[i] = frame[i];
        src.bytes[i] = frame[6U + i];
    }

    local = netif_get_mac();
    if (local == NULL) {
        return;
    }

    if (!mac_is_broadcast(&dst) && !mac_equals(&dst, local)) {
        return;
    }

    ethertype = eth_read_be16(&frame[12]);
    payload = frame + ETH_HDR_LEN;
    payload_len = (uint16_t)(length - ETH_HDR_LEN);

    if (ethertype == ETHERTYPE_ARP) {
        arp_input(payload, payload_len, &src);
    } else if (ethertype == ETHERTYPE_IPV4) {
        ipv4_input(payload, payload_len, &src);
    }
}

int ethernet_poll(void)
{
    int frames;
    static int soft_depth;
    netif_t *nif = netif_get_primary();

    if (nif != NULL) {
        frames = netif_poll(nif);
    } else {
        frames = e1000_poll_rx();
    }

    /*
     * Bottom-half for timers/HTTP. Kept out of the PIT ISR so loopback
     * TCP (self-wget) cannot race the same non-reentrant state.
     */
    if (soft_depth == 0) {
        soft_depth = 1;
        tcp_timer_tick();
        arp_age_tick();
        http_poll();
        soft_depth = 0;
    }
    return frames;
}

void net_bootstrap(void)
{
    uint32_t start;
    uint32_t rdt;
    mac_addr_t gateway_mac;
    int have_gateway = 0;
    netif_t *nif;
    char line[64];
    char ip_str[16];
    char gw_str[16];
    char dns_str[16];
    uint32_t i;
    uint32_t pos;

    route_init();
    udp_init();
    tcp_init();
    socket_init();
    dns_init();
    dhcp_init();
    http_init();

#ifdef TESTOS_TCP_SELFTEST
    if (!tcp_selftest()) {
        klog(KLOG_ERROR, "TCP", "Selftest failed");
        return;
    }
    klog(KLOG_INFO, "TCP", "Selftest passed");
#endif

    /*
     * QEMU's e1000 starts a 1000ms virtual-time flush timer on every RCTL
     * write and refuses ingress until it expires. Wait past that window.
     */
    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < (TIMER_FREQUENCY + 20U)) {
    }

    nif = netif_get_primary();
    if (nif != NULL) {
        if (!dhcp_request(nif, 5U * TIMER_FREQUENCY)) {
            netif_apply_static_defaults(nif);
            arp_set_local_ip(nif->ip);
            klog(KLOG_WARN, "NET", "DHCP failed; using static defaults");
        }
        route_set_defaults_from_netif(nif);
        arp_set_local_ip(nif->ip);
    }

    arp_probe_gateway();

    rdt = e1000_read_reg(E1000_REG_RDT);
    e1000_write_reg(E1000_REG_RDT, rdt);

    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < 50U) {
        rdt = e1000_read_reg(E1000_REG_RDT);
        e1000_write_reg(E1000_REG_RDT, rdt);
        (void)ethernet_poll();
        if (arp_lookup(netif_get_gateway() != 0 ? netif_get_gateway() : ARP_GATEWAY_IP_DEFAULT,
                       &gateway_mac)) {
            have_gateway = 1;
            break;
        }
    }

    nif = netif_get_primary();
    if (nif != NULL) {
        ipv4_addr_format(nif->ip, ip_str);
        ipv4_addr_format(nif->gateway, gw_str);
        ipv4_addr_format(nif->dns[0], dns_str);
        pos = 0;
        {
            static const char prefix[] = "e1000e0 ";
            for (i = 0; prefix[i] != '\0' && pos < sizeof(line) - 1U; i++) {
                line[pos++] = prefix[i];
            }
        }
        for (i = 0; ip_str[i] != '\0' && pos < sizeof(line) - 1U; i++) {
            line[pos++] = ip_str[i];
        }
        if (pos < sizeof(line) - 1U) {
            line[pos++] = ' ';
        }
        {
            static const char mid[] = "gw ";
            for (i = 0; mid[i] != '\0' && pos < sizeof(line) - 1U; i++) {
                line[pos++] = mid[i];
            }
        }
        for (i = 0; gw_str[i] != '\0' && pos < sizeof(line) - 1U; i++) {
            line[pos++] = gw_str[i];
        }
        if (pos < sizeof(line) - 1U) {
            line[pos++] = ' ';
        }
        {
            static const char mid[] = "dns ";
            for (i = 0; mid[i] != '\0' && pos < sizeof(line) - 1U; i++) {
                line[pos++] = mid[i];
            }
        }
        for (i = 0; dns_str[i] != '\0' && pos < sizeof(line) - 1U; i++) {
            line[pos++] = dns_str[i];
        }
        line[pos] = '\0';
        klog(KLOG_INFO, "NET", line);
    }

    if (!have_gateway) {
        klog(KLOG_WARN, "NET", "No ARP reply yet (try netrx)");
        return;
    }

#ifdef TESTOS_TCP_ACTIVE_TEST
#ifdef TESTOS_TCP_TEST_HOOKS
    tcp_test_drop_next_segment();
    tcp_test_drop_next_payload();
#endif
    (void)tcp_connect(netif_get_gateway(), 12347U, 40000U);
#endif

    {
        ipv4_addr_t gw = netif_get_gateway();
        if (gw == 0U) {
            gw = ARP_GATEWAY_IP_DEFAULT;
        }
        icmp_arm_echo_wait(gw, 0x1234U, 1U);
        (void)icmp_send_echo_request(gw, 0x1234U, 1U);
    }

    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < 50U) {
        rdt = e1000_read_reg(E1000_REG_RDT);
        e1000_write_reg(E1000_REG_RDT, rdt);
        (void)ethernet_poll();
    }
}
