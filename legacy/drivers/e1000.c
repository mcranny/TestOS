#include "e1000.h"
#include "pci.h"
#include "paging.h"
#include "pmm.h"
#include "memory.h"
#include "pic.h"
#include "interrupts.h"
#include "log.h"
#include "ethernet.h"

#define E1000_RESET_TIMEOUT    1000000U
#define E1000_POST_RESET_SPIN  10000U
#define E1000_TX_POLL_TIMEOUT  1000000U
#define E1000_MTA_DWORDS       128U
#define PIC_CASCADE_IRQ        2U
#define E1000_ETH_MAX_LEN      1514U

extern void e1000_irq_stub(void);

static volatile uint32_t *e1000_mmio = NULL;
static mac_address_t e1000_mac;
static int e1000_ready = 0;
static uint8_t e1000_irq = 0xFFU;
static int e1000_irq_logged = 0;

static volatile e1000_tx_desc_t *tx_ring = NULL;
static uint8_t *tx_buffer = NULL;
static uint32_t tx_ring_phys = 0;
static uint32_t tx_buffer_phys = 0;
static uint16_t tx_tail = 0;
static int tx_configured = 0;

static volatile e1000_rx_desc_t *rx_ring = NULL;
static uint8_t *rx_buffers[E1000_RX_DESC_COUNT];
static uint32_t rx_buffer_phys[E1000_RX_DESC_COUNT];
static uint32_t rx_ring_phys = 0;
static uint16_t rx_next = 0;
static int rx_configured = 0;

uint32_t e1000_read_reg(uint32_t offset)
{
    if (e1000_mmio == NULL)
    {
        return 0;
    }

    return e1000_mmio[offset / 4U];
}

void e1000_write_reg(uint32_t offset, uint32_t value)
{
    if (e1000_mmio == NULL)
    {
        return;
    }

    e1000_mmio[offset / 4U] = value;
}

const mac_address_t *e1000_get_mac(void)
{
    if (!e1000_ready)
    {
        return NULL;
    }

    return &e1000_mac;
}

static void e1000_hex_nibble(uint8_t nibble, char *out)
{
    static const char hex_digits[] = "0123456789abcdef";
    *out = hex_digits[nibble & 0x0FU];
}

static void e1000_format_mac(char *line)
{
    uint32_t i;
    uint32_t pos = 0;

    line[pos++] = 'M';
    line[pos++] = 'A';
    line[pos++] = 'C';
    line[pos++] = ' ';
    line[pos++] = '=';
    line[pos++] = ' ';

    for (i = 0; i < 6U; i++)
    {
        e1000_hex_nibble((uint8_t)(e1000_mac.bytes[i] >> 4), &line[pos]);
        pos++;
        e1000_hex_nibble(e1000_mac.bytes[i], &line[pos]);
        pos++;

        if (i < 5U)
        {
            line[pos++] = ':';
        }
    }

    line[pos] = '\0';
}

static void e1000_format_cause(char *line, uint32_t cause)
{
    uint32_t pos = 0;
    int shift;

    line[pos++] = 'C';
    line[pos++] = 'a';
    line[pos++] = 'u';
    line[pos++] = 's';
    line[pos++] = 'e';
    line[pos++] = ' ';
    line[pos++] = '=';
    line[pos++] = ' ';
    line[pos++] = '0';
    line[pos++] = 'x';

    for (shift = 28; shift >= 0; shift -= 4)
    {
        e1000_hex_nibble((uint8_t)((cause >> shift) & 0xFU), &line[pos]);
        pos++;
    }

    line[pos] = '\0';
}

static int e1000_reset(void)
{
    uint32_t ctrl;
    uint32_t i;

    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFFU);

    ctrl = e1000_read_reg(E1000_REG_CTRL);
    e1000_write_reg(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);

    for (i = 0; i < E1000_RESET_TIMEOUT; i++)
    {
        if ((e1000_read_reg(E1000_REG_CTRL) & E1000_CTRL_RST) == 0)
        {
            break;
        }
    }

    if (i >= E1000_RESET_TIMEOUT)
    {
        klog(KLOG_ERROR, "E1000", "Reset timed out");
        return 0;
    }

    for (i = 0; i < E1000_POST_RESET_SPIN; i++)
    {
        __asm__ volatile ("nop");
    }

    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFFU);
    klog(KLOG_INFO, "E1000", "Reset complete");
    return 1;
}

static void e1000_read_mac(void)
{
    uint32_t ral;
    uint32_t rah;

    ral = e1000_read_reg(E1000_REG_RAL0);
    rah = e1000_read_reg(E1000_REG_RAH0);

    e1000_mac.bytes[0] = (uint8_t)(ral & 0xFFU);
    e1000_mac.bytes[1] = (uint8_t)((ral >> 8) & 0xFFU);
    e1000_mac.bytes[2] = (uint8_t)((ral >> 16) & 0xFFU);
    e1000_mac.bytes[3] = (uint8_t)((ral >> 24) & 0xFFU);
    e1000_mac.bytes[4] = (uint8_t)(rah & 0xFFU);
    e1000_mac.bytes[5] = (uint8_t)((rah >> 8) & 0xFFU);
}

static void e1000_program_mac_filter(void)
{
    uint32_t ral;
    uint32_t rah;

    ral = (uint32_t)e1000_mac.bytes[0] |
          ((uint32_t)e1000_mac.bytes[1] << 8) |
          ((uint32_t)e1000_mac.bytes[2] << 16) |
          ((uint32_t)e1000_mac.bytes[3] << 24);
    rah = (uint32_t)e1000_mac.bytes[4] |
          ((uint32_t)e1000_mac.bytes[5] << 8) |
          E1000_RAH_AV;

    e1000_write_reg(E1000_REG_RAL0, ral);
    e1000_write_reg(E1000_REG_RAH0, rah);
}

static int e1000_tx_init(void)
{
    uint32_t ring_phys;
    uint32_t buffer_phys;
    uint32_t tctl;

    ring_phys = pmm_alloc_frame();
    if (ring_phys == 0)
    {
        klog(KLOG_ERROR, "E1000", "Failed to allocate TX ring");
        return 0;
    }

    buffer_phys = pmm_alloc_frame();
    if (buffer_phys == 0)
    {
        klog(KLOG_ERROR, "E1000", "Failed to allocate TX buffer");
        pmm_free_frame(ring_phys);
        return 0;
    }

    tx_ring = (volatile e1000_tx_desc_t *)ring_phys;
    tx_buffer = (uint8_t *)buffer_phys;
    tx_ring_phys = ring_phys;
    tx_buffer_phys = buffer_phys;
    tx_tail = 0;

    memset((void *)tx_ring, 0, PAGE_SIZE);
    memset(tx_buffer, 0, PAGE_SIZE);

    e1000_write_reg(E1000_REG_TDBAL, tx_ring_phys);
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, E1000_TX_DESC_COUNT * (uint32_t)sizeof(e1000_tx_desc_t));
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);

    /* IPGT=10, IPGR1=10, IPGR2=10 */
    e1000_write_reg(E1000_REG_TIPG, 10U | (10U << 10) | (10U << 20));

    tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10U << 4) | (0x40U << 12);
    e1000_write_reg(E1000_REG_TCTL, tctl);

    tx_configured = 1;
    klog(KLOG_INFO, "E1000", "TX configured");
    return 1;
}

static void e1000_set_rctl(void)
{
    uint32_t rctl;

    rctl = E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE | E1000_RCTL_BAM |
           E1000_RCTL_SECRC | E1000_RCTL_BSIZE_2048;
    e1000_write_reg(E1000_REG_RCTL, rctl);
    e1000_write_reg(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1U);
}

static int e1000_rx_init(void)
{
    uint32_t ring_phys;
    uint32_t i;
    uint32_t ctrl;

    e1000_write_reg(E1000_REG_RCTL, 0);

    ring_phys = pmm_alloc_frame();
    if (ring_phys == 0)
    {
        klog(KLOG_ERROR, "E1000", "Failed to allocate RX ring");
        return 0;
    }

    rx_ring = (volatile e1000_rx_desc_t *)ring_phys;
    rx_ring_phys = ring_phys;
    memset((void *)rx_ring, 0, PAGE_SIZE);

    for (i = 0; i < E1000_RX_DESC_COUNT; i++)
    {
        uint32_t buf_phys = pmm_alloc_frame();

        if (buf_phys == 0)
        {
            klog(KLOG_ERROR, "E1000", "Failed to allocate RX buffer");
            return 0;
        }

        rx_buffers[i] = (uint8_t *)buf_phys;
        rx_buffer_phys[i] = buf_phys;
        memset(rx_buffers[i], 0, PAGE_SIZE);

        rx_ring[i].addr_lo = buf_phys;
        rx_ring[i].addr_hi = 0;
        rx_ring[i].length = 0;
        rx_ring[i].checksum = 0;
        rx_ring[i].status = 0;
        rx_ring[i].errors = 0;
        rx_ring[i].special = 0;
    }

    for (i = 0; i < E1000_MTA_DWORDS; i++)
    {
        e1000_write_reg(E1000_REG_MTA + (i * 4U), 0);
    }

    e1000_program_mac_filter();

    e1000_write_reg(E1000_REG_RDBAL, rx_ring_phys);
    e1000_write_reg(E1000_REG_RDBAH, 0);
    e1000_write_reg(E1000_REG_RDLEN, E1000_RX_DESC_COUNT * (uint32_t)sizeof(e1000_rx_desc_t));
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1U);

    rx_next = 0;

    ctrl = e1000_read_reg(E1000_REG_CTRL);
    e1000_write_reg(E1000_REG_CTRL, ctrl | E1000_CTRL_SLU | E1000_CTRL_ASDE);

    e1000_set_rctl();

    rx_configured = 1;
    klog(KLOG_INFO, "E1000", "RX configured");
    return 1;
}

int e1000_poll_rx(void)
{
    int frames = 0;

    if (!e1000_ready || !rx_configured || rx_ring == NULL)
    {
        return 0;
    }

    while (frames < (int)E1000_RX_DESC_COUNT)
    {
        volatile e1000_rx_desc_t *desc = &rx_ring[rx_next];
        uint16_t length;
        uint8_t *buf;
        uint16_t consumed;

        if ((desc->status & E1000_RXD_STAT_DD) == 0)
        {
            break;
        }

        length = desc->length;
        buf = rx_buffers[rx_next];

        if (length >= ETH_HDR_LEN && buf != NULL)
        {
            ethernet_input(buf, length);
        }

        desc->status = 0;
        desc->length = 0;
        desc->errors = 0;

        consumed = rx_next;
        rx_next = (uint16_t)((rx_next + 1U) % E1000_RX_DESC_COUNT);
        e1000_write_reg(E1000_REG_RDT, consumed);

        frames++;
    }

    return frames;
}

int e1000_transmit(const void *data, uint16_t length)
{
    uint16_t desc_index;
    uint16_t next_tail;
    uint32_t i;
    volatile e1000_tx_desc_t *desc;

    if (!e1000_ready || !tx_configured || tx_ring == NULL || tx_buffer == NULL)
    {
        return 0;
    }

    if (data == NULL || length == 0 || length > E1000_ETH_MAX_LEN)
    {
        return 0;
    }

    desc_index = tx_tail;
    next_tail = (uint16_t)((desc_index + 1U) % E1000_TX_DESC_COUNT);

    memcpy(tx_buffer, data, length);

    desc = &tx_ring[desc_index];
    desc->buffer_addr = (uint64_t)tx_buffer_phys;
    desc->length = length;
    desc->cso = 0;
    desc->cmd = (uint8_t)(E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS);
    desc->status = 0;
    desc->css = 0;
    desc->special = 0;

    tx_tail = next_tail;
    e1000_write_reg(E1000_REG_TDT, next_tail);

    klog(KLOG_INFO, "E1000", "Transmitting frame");

    for (i = 0; i < E1000_TX_POLL_TIMEOUT; i++)
    {
        if ((desc->status & E1000_TXD_STAT_DD) != 0)
        {
            klog(KLOG_INFO, "E1000", "TX complete");
            return 1;
        }
    }

    klog(KLOG_ERROR, "E1000", "TX timeout");
    return 0;
}

#define E1000_ETHERTYPE_TEST 0x88B5U

static void e1000_send_test_frame(void)
{
    mac_addr_t broadcast;
    static const char payload[] = "TestOS-TX";

    mac_set_broadcast(&broadcast);
    (void)ethernet_send(
        &broadcast,
        E1000_ETHERTYPE_TEST,
        payload,
        (uint16_t)(sizeof(payload) - 1U)
    );
}

void e1000_disable_interrupts(void)
{
    if (e1000_mmio == NULL)
    {
        return;
    }

    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFFU);
    (void)e1000_read_reg(E1000_REG_ICR);
}

void e1000_enable_interrupts(void)
{
    if (!e1000_ready || e1000_irq > 15U)
    {
        return;
    }

    /* Clear any pending cause bits. */
    (void)e1000_read_reg(E1000_REG_ICR);

    /* Enable a single controlled source for path verification. */
    e1000_write_reg(E1000_REG_IMS, E1000_ICR_LSC);

    if (e1000_irq >= 8U)
    {
        pic_unmask_irq(PIC_CASCADE_IRQ);
    }

    pic_unmask_irq(e1000_irq);
    klog(KLOG_INFO, "E1000", "Interrupts enabled");

    /* Force a link-status-change interrupt via ICS. */
    e1000_write_reg(E1000_REG_ICS, E1000_ICR_LSC);
}

void e1000_irq_handler(void)
{
    uint32_t cause;
    char cause_line[24];

    cause = e1000_read_reg(E1000_REG_ICR);

    if (!e1000_irq_logged)
    {
        e1000_irq_logged = 1;
        klog(KLOG_INFO, "E1000", "IRQ received");
        e1000_format_cause(cause_line, cause);
        klog(KLOG_INFO, "E1000", cause_line);
    }

    if (e1000_irq <= 15U)
    {
        pic_send_eoi(e1000_irq);
    }

    /* Poll RX to process any received frames delivered by the NIC. */
    (void)e1000_poll_rx();
}

void e1000_initialize(void)
{
    device_t *dev;
    pci_bar_info_t bar;
    uint16_t command;
    void *mapped;
    uint32_t ctrl;
    uint32_t status;
    char mac_line[32];

    e1000_ready = 0;
    e1000_irq = 0xFFU;
    e1000_irq_logged = 0;
    tx_configured = 0;
    rx_configured = 0;

    klog(KLOG_INFO, "E1000", "Initializing Intel E1000");

    dev = pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID);
    if (dev == NULL)
    {
        klog(KLOG_WARN, "E1000", "Device not found");
        return;
    }

    klog(KLOG_INFO, "E1000", "Found Intel E1000 NIC");

    if (!pci_decode_bar(dev, 0, &bar))
    {
        klog(KLOG_ERROR, "E1000", "Failed to decode BAR0");
        return;
    }

    klog(KLOG_INFO, "E1000", "BAR0 detected");

    if (bar.is_io)
    {
        klog(KLOG_ERROR, "E1000", "BAR0 type = I/O (expected MMIO)");
        return;
    }

    klog(KLOG_INFO, "E1000", "BAR0 type = MMIO");
    klog_uint(KLOG_INFO, "E1000", "BAR0 physical base = ", bar.phys_addr);

    if (bar.phys_addr == 0 || bar.size == 0)
    {
        klog(KLOG_ERROR, "E1000", "BAR0 MMIO region invalid");
        return;
    }

    klog_uint(KLOG_INFO, "E1000", "MMIO base = ", bar.phys_addr);
    klog_uint(KLOG_INFO, "E1000", "MMIO size = ", bar.size);

    pci_enable_bus_mastering(dev);
    command = pci_read16(dev->bus, dev->device, dev->function, PCI_COMMAND);
    klog_uint(KLOG_INFO, "E1000", "PCI command = ", command);

    mapped = paging_map_mmio((uintptr_t)bar.phys_addr, bar.size);
    if (mapped == NULL)
    {
        klog(KLOG_ERROR, "E1000", "Failed to map MMIO");
        return;
    }

    e1000_mmio = (volatile uint32_t *)mapped;
    klog(KLOG_INFO, "E1000", "MMIO mapped");

    if (!e1000_reset())
    {
        return;
    }

    ctrl = e1000_read_reg(E1000_REG_CTRL);
    status = e1000_read_reg(E1000_REG_STATUS);
    klog_uint(KLOG_INFO, "E1000", "CTRL = ", ctrl);
    klog_uint(KLOG_INFO, "E1000", "STATUS = ", status);

    e1000_read_mac();
    e1000_format_mac(mac_line);
    klog(KLOG_INFO, "E1000", mac_line);

    status = e1000_read_reg(E1000_REG_STATUS);
    if ((status & E1000_STATUS_LU) != 0)
    {
        klog(KLOG_INFO, "E1000", "Link up");
    }
    else
    {
        klog(KLOG_WARN, "E1000", "Link down");
    }

    if (!e1000_tx_init())
    {
        return;
    }

    if (!e1000_rx_init())
    {
        return;
    }

    e1000_irq = dev->interrupt_line;
    klog_uint(KLOG_INFO, "E1000", "PCI interrupt line = IRQ ", e1000_irq);

    if (e1000_irq > 15U)
    {
        klog(KLOG_ERROR, "E1000", "Invalid PCI interrupt line");
        return;
    }

    interrupts_register_irq(e1000_irq, e1000_irq_stub);
    klog(KLOG_INFO, "E1000", "Interrupt handler installed");

    e1000_ready = 1;
    klog(KLOG_INFO, "E1000", "NIC initialized");

    e1000_send_test_frame();
    e1000_send_test_frame();

    /*
     * Arm device + PIC now. The forced ICS interrupt becomes pending and
     * is delivered when the kernel later executes STI.
     */
    e1000_enable_interrupts();
}
