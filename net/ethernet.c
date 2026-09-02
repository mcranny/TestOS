#include "ethernet.h"
#include "arp.h"
#include "ipv4.h"
#include "icmp.h"
#include "udp.h"
#include "e1000.h"
#include "log.h"
#include "memory.h"
#include "timer.h"

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
    const mac_address_t *local;
    uint16_t total;
    uint16_t i;

    if (dst == NULL || (payload == NULL && payload_len != 0) ||
        payload_len > ETH_MAX_PAYLOAD)
    {
        return 0;
    }

    local = e1000_get_mac();
    if (local == NULL)
    {
        return 0;
    }

    for (i = 0; i < 6U; i++)
    {
        frame[i] = dst->bytes[i];
        frame[6U + i] = local->bytes[i];
    }

    eth_write_be16(&frame[12], ethertype);

    if (payload_len > 0U)
    {
        memcpy(&frame[ETH_HDR_LEN], payload, payload_len);
    }

    total = (uint16_t)(ETH_HDR_LEN + payload_len);
    if (total < ETH_MIN_FRAME)
    {
        memset(&frame[total], 0, (size_t)(ETH_MIN_FRAME - total));
        total = ETH_MIN_FRAME;
    }

    return e1000_transmit(frame, total);
}

void ethernet_input(const uint8_t *frame, uint16_t length)
{
    mac_addr_t dst;
    mac_addr_t src;
    mac_addr_t local_mac;
    const mac_address_t *local;
    uint16_t ethertype;
    const uint8_t *payload;
    uint16_t payload_len;
    uint32_t i;

    if (frame == NULL || length < ETH_HDR_LEN)
    {
        return;
    }

    for (i = 0; i < 6U; i++)
    {
        dst.bytes[i] = frame[i];
        src.bytes[i] = frame[6U + i];
    }

    local = e1000_get_mac();
    if (local == NULL)
    {
        return;
    }

    for (i = 0; i < 6U; i++)
    {
        local_mac.bytes[i] = local->bytes[i];
    }

    if (!mac_is_broadcast(&dst) && !mac_equals(&dst, &local_mac))
    {
        return;
    }

    ethertype = eth_read_be16(&frame[12]);
    payload = frame + ETH_HDR_LEN;
    payload_len = (uint16_t)(length - ETH_HDR_LEN);

    klog(KLOG_INFO, "NET", "RX frame");
    klog_uint(KLOG_INFO, "NET", "Length = ", length);

    if (ethertype == ETHERTYPE_ARP)
    {
        arp_input(payload, payload_len, &src);
    }
    else if (ethertype == ETHERTYPE_IPV4)
    {
        ipv4_input(payload, payload_len, &src);
    }
    else
    {
        klog_uint(KLOG_INFO, "NET", "Unknown EtherType = ", ethertype);
    }
}

int ethernet_poll(void)
{
    return e1000_poll_rx();
}

void net_bootstrap(void)
{
    uint32_t start;
    uint32_t rdt;
    mac_addr_t gateway_mac;
    int have_gateway = 0;

    udp_init();

    /*
     * QEMU's e1000 starts a 1000ms virtual-time flush timer on every RCTL
     * write and refuses ingress until it expires. Wait past that window.
     */
    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < (TIMER_FREQUENCY + 20U))
    {
    }

    arp_probe_gateway();

    /* Touch RDT to encourage QEMU to flush queued ingress. */
    rdt = e1000_read_reg(E1000_REG_RDT);
    e1000_write_reg(E1000_REG_RDT, rdt);

    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < 50U)
    {
        rdt = e1000_read_reg(E1000_REG_RDT);
        e1000_write_reg(E1000_REG_RDT, rdt);

        (void)ethernet_poll();

        if (arp_lookup(ARP_GATEWAY_IP_DEFAULT, &gateway_mac))
        {
            have_gateway = 1;
            break;
        }
    }

    if (!have_gateway)
    {
        klog(KLOG_WARN, "NET", "No ARP reply yet (try netrx)");
        return;
    }

    icmp_arm_echo_wait(ARP_GATEWAY_IP_DEFAULT, 0x1234U, 1U);
    (void)icmp_send_echo_request(ARP_GATEWAY_IP_DEFAULT, 0x1234U, 1U);

    start = timer_get_ticks();
    while ((timer_get_ticks() - start) < 50U)
    {
        rdt = e1000_read_reg(E1000_REG_RDT);
        e1000_write_reg(E1000_REG_RDT, rdt);
        (void)ethernet_poll();
    }
}
