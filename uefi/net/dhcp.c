#include "dhcp.h"
#include "udp.h"
#include "ipv4.h"
#include "arp.h"
#include "ethernet.h"
#include "netif.h"
#include "route.h"
#include "mac.h"
#include "platform.h"
#include "lib/string.h"

#define DHCP_SERVER_PORT 67U
#define DHCP_CLIENT_PORT 68U
#define DHCP_OP_BOOTREQUEST 1U
#define DHCP_OP_BOOTREPLY   2U
#define DHCP_MAGIC 0x63825363U
#define DHCP_OPT_PAD 0U
#define DHCP_OPT_SUBNET 1U
#define DHCP_OPT_ROUTER 3U
#define DHCP_OPT_DNS 6U
#define DHCP_OPT_REQ_IP 50U
#define DHCP_OPT_LEASE 51U
#define DHCP_OPT_MSGTYPE 53U
#define DHCP_OPT_SERVER 54U
#define DHCP_OPT_PARAM_REQ 55U
#define DHCP_OPT_END 255U
#define DHCP_DISCOVER 1U
#define DHCP_OFFER 2U
#define DHCP_REQUEST 3U
#define DHCP_ACK 5U
#define DHCP_NAK 6U

#define DHCP_XID 0x54455354U /* 'TEST' */

typedef enum {
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_SELECTING,
    DHCP_STATE_REQUESTING,
    DHCP_STATE_BOUND,
    DHCP_STATE_FAILED
} dhcp_state_t;

static dhcp_state_t dhcp_state;
static ipv4_addr_t dhcp_offer_ip;
static ipv4_addr_t dhcp_server_ip;
static ipv4_addr_t dhcp_offer_mask;
static ipv4_addr_t dhcp_offer_gw;
static ipv4_addr_t dhcp_offer_dns0;
static ipv4_addr_t dhcp_offer_dns1;
static netif_t *dhcp_nif;
static int dhcp_bound;

static void dhcp_write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)((v >> 24) & 0xFFU);
    p[1] = (uint8_t)((v >> 16) & 0xFFU);
    p[2] = (uint8_t)((v >> 8) & 0xFFU);
    p[3] = (uint8_t)(v & 0xFFU);
}

static uint32_t dhcp_read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int dhcp_build_message(uint8_t *buf, uint16_t *len_out, uint8_t msg_type, ipv4_addr_t req_ip)
{
    const mac_addr_t *mac;
    uint16_t pos;
    uint32_t i;

    if (buf == NULL || len_out == NULL) {
        return 0;
    }
    mac = netif_get_mac();
    if (mac == NULL) {
        return 0;
    }

    memset(buf, 0, 300);
    buf[0] = DHCP_OP_BOOTREQUEST;
    buf[1] = 1; /* HTYPE ethernet */
    buf[2] = 6; /* HLEN */
    buf[3] = 0; /* hops */
    dhcp_write_be32(&buf[4], DHCP_XID);
    /* secs, flags */
    buf[10] = 0x80; /* broadcast flag */
    buf[11] = 0x00;
    for (i = 0; i < 6U; i++) {
        buf[28U + i] = mac->bytes[i];
    }
    /* magic cookie */
    dhcp_write_be32(&buf[236], DHCP_MAGIC);

    pos = 240;
    buf[pos++] = DHCP_OPT_MSGTYPE;
    buf[pos++] = 1;
    buf[pos++] = msg_type;

    if (msg_type == DHCP_REQUEST && req_ip != 0U) {
        buf[pos++] = DHCP_OPT_REQ_IP;
        buf[pos++] = 4;
        ipv4_addr_to_bytes(req_ip, &buf[pos]);
        pos = (uint16_t)(pos + 4U);
        if (dhcp_server_ip != 0U) {
            buf[pos++] = DHCP_OPT_SERVER;
            buf[pos++] = 4;
            ipv4_addr_to_bytes(dhcp_server_ip, &buf[pos]);
            pos = (uint16_t)(pos + 4U);
        }
    }

    buf[pos++] = DHCP_OPT_PARAM_REQ;
    buf[pos++] = 3;
    buf[pos++] = DHCP_OPT_SUBNET;
    buf[pos++] = DHCP_OPT_ROUTER;
    buf[pos++] = DHCP_OPT_DNS;

    buf[pos++] = DHCP_OPT_END;
    while (pos < 300U) {
        buf[pos++] = 0;
    }
    *len_out = 300;
    return 1;
}

static void dhcp_parse_options(const uint8_t *opts, uint16_t len)
{
    uint16_t i = 0;

    dhcp_offer_mask = 0;
    dhcp_offer_gw = 0;
    dhcp_offer_dns0 = 0;
    dhcp_offer_dns1 = 0;

    while (i < len) {
        uint8_t code = opts[i++];
        uint8_t olen;
        if (code == DHCP_OPT_PAD) {
            continue;
        }
        if (code == DHCP_OPT_END || i >= len) {
            break;
        }
        olen = opts[i++];
        if ((uint16_t)(i + olen) > len) {
            break;
        }
        if (code == DHCP_OPT_MSGTYPE && olen >= 1) {
            /* handled by caller via return path */
        } else if (code == DHCP_OPT_SUBNET && olen >= 4) {
            dhcp_offer_mask = ipv4_addr_from_bytes(&opts[i]);
        } else if (code == DHCP_OPT_ROUTER && olen >= 4) {
            dhcp_offer_gw = ipv4_addr_from_bytes(&opts[i]);
        } else if (code == DHCP_OPT_DNS && olen >= 4) {
            dhcp_offer_dns0 = ipv4_addr_from_bytes(&opts[i]);
            if (olen >= 8) {
                dhcp_offer_dns1 = ipv4_addr_from_bytes(&opts[i + 4]);
            }
        } else if (code == DHCP_OPT_SERVER && olen >= 4) {
            dhcp_server_ip = ipv4_addr_from_bytes(&opts[i]);
        }
        i = (uint16_t)(i + olen);
    }
}

static uint8_t dhcp_msg_type_from_options(const uint8_t *opts, uint16_t len)
{
    uint16_t i = 0;

    while (i < len) {
        uint8_t code = opts[i++];
        uint8_t olen;
        if (code == DHCP_OPT_PAD) {
            continue;
        }
        if (code == DHCP_OPT_END || i >= len) {
            break;
        }
        olen = opts[i++];
        if ((uint16_t)(i + olen) > len) {
            break;
        }
        if (code == DHCP_OPT_MSGTYPE && olen >= 1) {
            return opts[i];
        }
        i = (uint16_t)(i + olen);
    }
    return 0;
}

static void dhcp_on_udp(
    ipv4_addr_t src_ip,
    uint16_t src_port,
    ipv4_addr_t dst_ip,
    uint16_t dst_port,
    const uint8_t *data,
    uint16_t data_len
)
{
    uint32_t xid;
    uint8_t msg;
    ipv4_addr_t yiaddr;

    (void)dst_ip;
    (void)dst_port;

    if (src_port != DHCP_SERVER_PORT || data == NULL || data_len < 240U) {
        return;
    }
    if (data[0] != DHCP_OP_BOOTREPLY) {
        return;
    }
    xid = dhcp_read_be32(&data[4]);
    if (xid != DHCP_XID) {
        return;
    }
    if (dhcp_read_be32(&data[236]) != DHCP_MAGIC) {
        return;
    }

    yiaddr = ipv4_addr_from_bytes(&data[16]);
    msg = dhcp_msg_type_from_options(&data[240], (uint16_t)(data_len - 240U));
    dhcp_parse_options(&data[240], (uint16_t)(data_len - 240U));
    if (dhcp_server_ip == 0U) {
        dhcp_server_ip = src_ip;
    }

    if (msg == DHCP_OFFER && dhcp_state == DHCP_STATE_SELECTING) {
        dhcp_offer_ip = yiaddr;
        dhcp_state = DHCP_STATE_REQUESTING;
        klog(KLOG_INFO, "DHCP", "Offer received");
    } else if (msg == DHCP_ACK &&
               (dhcp_state == DHCP_STATE_REQUESTING || dhcp_state == DHCP_STATE_SELECTING)) {
        dhcp_offer_ip = yiaddr;
        dhcp_state = DHCP_STATE_BOUND;
        dhcp_bound = 1;
        klog(KLOG_INFO, "DHCP", "ACK received");
    } else if (msg == DHCP_NAK) {
        dhcp_state = DHCP_STATE_FAILED;
        klog(KLOG_WARN, "DHCP", "NAK received");
    }
}

static int dhcp_send(uint8_t msg_type, ipv4_addr_t req_ip)
{
    uint8_t buf[300];
    uint16_t len = 0;

    if (!dhcp_build_message(buf, &len, msg_type, req_ip)) {
        return 0;
    }
    return udp_send(IPV4_ADDR(255, 255, 255, 255), DHCP_SERVER_PORT, DHCP_CLIENT_PORT, buf, len);
}

static void dhcp_apply(netif_t *nif)
{
    ipv4_addr_t mask = dhcp_offer_mask != 0U ? dhcp_offer_mask : IPV4_ADDR(255, 255, 255, 0);
    ipv4_addr_t gw = dhcp_offer_gw != 0U ? dhcp_offer_gw : IPV4_ADDR(10, 0, 2, 2);
    ipv4_addr_t dns0 = dhcp_offer_dns0 != 0U ? dhcp_offer_dns0 : IPV4_ADDR(10, 0, 2, 3);

    if (nif == NULL || dhcp_offer_ip == 0U) {
        return;
    }
    netif_set_addr(nif, dhcp_offer_ip, mask, gw);
    netif_set_dns(nif, dns0, dhcp_offer_dns1);
    netif_add_flags(nif, NETIF_FLAG_DHCP);
    arp_set_local_ip(dhcp_offer_ip);
    route_set_defaults_from_netif(nif);
}

void dhcp_init(void)
{
    dhcp_state = DHCP_STATE_IDLE;
    dhcp_offer_ip = 0;
    dhcp_server_ip = 0;
    dhcp_offer_mask = 0;
    dhcp_offer_gw = 0;
    dhcp_offer_dns0 = 0;
    dhcp_offer_dns1 = 0;
    dhcp_nif = NULL;
    dhcp_bound = 0;
    (void)udp_bind(DHCP_CLIENT_PORT, dhcp_on_udp);
}

int dhcp_is_bound(void)
{
    return dhcp_bound;
}

int dhcp_request(netif_t *nif, uint32_t timeout_ticks)
{
    uint32_t start;
    uint32_t last_tx;
    uint8_t phase;

    if (nif == NULL) {
        return 0;
    }

    dhcp_nif = nif;
    dhcp_bound = 0;
    dhcp_state = DHCP_STATE_SELECTING;
    dhcp_offer_ip = 0;
    dhcp_server_ip = 0;

    /* Temporarily use 0.0.0.0 as source for discover. */
    arp_set_local_ip(0);
    nif->ip = 0;

    if (!dhcp_send(DHCP_DISCOVER, 0)) {
        dhcp_state = DHCP_STATE_FAILED;
        return 0;
    }
    last_tx = timer_get_ticks();
    start = last_tx;
    phase = DHCP_DISCOVER;

    while ((timer_get_ticks() - start) < timeout_ticks) {
        (void)ethernet_poll();

        if (dhcp_state == DHCP_STATE_REQUESTING && phase == DHCP_DISCOVER) {
            phase = DHCP_REQUEST;
            (void)dhcp_send(DHCP_REQUEST, dhcp_offer_ip);
            last_tx = timer_get_ticks();
        }

        if (dhcp_state == DHCP_STATE_BOUND) {
            dhcp_apply(nif);
            return 1;
        }
        if (dhcp_state == DHCP_STATE_FAILED) {
            return 0;
        }

        if ((timer_get_ticks() - last_tx) > (TIMER_FREQUENCY / 2U)) {
            if (phase == DHCP_DISCOVER) {
                (void)dhcp_send(DHCP_DISCOVER, 0);
            } else {
                (void)dhcp_send(DHCP_REQUEST, dhcp_offer_ip);
            }
            last_tx = timer_get_ticks();
        }
    }

    dhcp_state = DHCP_STATE_FAILED;
    return 0;
}

int dhcp_renew(netif_t *nif)
{
    if (nif == NULL) {
        nif = netif_get_primary();
    }
    return dhcp_request(nif, 5U * TIMER_FREQUENCY);
}
