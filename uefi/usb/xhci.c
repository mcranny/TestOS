#include "usb/xhci.h"
#include "usb/hid.h"
#include "drivers/pci.h"
#include "drivers/device.h"
#include "mm/dma.h"
#include "platform.h"
#include "lib/string.h"

#define XHCI_TIMEOUT             1000000U
#define XHCI_CMD_RING_TRBS         64U
#define XHCI_EVT_RING_TRBS         64U
#define XHCI_MAX_SLOTS             64U
#define XHCI_CONTEXT_SIZE          2048U

#define XHCI_TRB_TYPE_NORMAL       1U
#define XHCI_TRB_TYPE_SETUP        2U
#define XHCI_TRB_TYPE_DATA         3U
#define XHCI_TRB_TYPE_STATUS       4U
#define XHCI_TRB_TYPE_LINK         6U
#define XHCI_TRB_TYPE_NOOP         8U
#define XHCI_TRB_TYPE_EN_SLOT      9U
#define XHCI_TRB_TYPE_ADDR_DEV     11U
#define XHCI_TRB_TYPE_CONF_EP      12U
#define XHCI_TRB_TYPE_XFER_EVT     32U
#define XHCI_TRB_TYPE_CMD_CMPL     33U

#define XHCI_PORTSC_CCS            (1U << 0)
#define XHCI_PORTSC_PED            (1U << 1)
#define XHCI_PORTSC_PR             (1U << 4)
#define XHCI_PORTSC_PP             (1U << 9)

#define XHCI_USBCMD_RUN            (1U << 0)
#define XHCI_USBCMD_HCRST          (1U << 1)
#define XHCI_USBSTS_HCH            (1U << 0)

typedef struct xhci_trb
{
    uint32_t parameter;
    uint32_t status;
    uint32_t control;
    uint32_t rsv;
} __attribute__((packed)) xhci_trb_t;

typedef struct xhci_erst_entry
{
    uint64_t ring_phys;
    uint32_t ring_size;
    uint32_t rsv;
} __attribute__((packed)) xhci_erst_entry_t;

typedef struct xhci_regs
{
    volatile uint8_t *base;
    uint32_t cap_length;
    uint32_t db_offset;
    uint32_t rt_offset;
    uint32_t max_ports;
    uint32_t max_slots;
    uint32_t doorbell_stride;
} xhci_regs_t;

static xhci_regs_t xhci;
static dma_buffer_t dcbaa;
static dma_buffer_t cmd_ring_buf;
static dma_buffer_t evt_ring_buf;
static dma_buffer_t erst_buf;
static dma_buffer_t scratch_buf;
static uint32_t cmd_ring_index;
static uint32_t evt_ring_index;
static uint32_t evt_ring_cycle;
static int xhci_ready;

static volatile uint32_t *xhci_op(uint32_t offset)
{
    return (volatile uint32_t *)(xhci.base + xhci.cap_length + offset);
}

static volatile uint32_t *xhci_rt(uint32_t offset)
{
    return (volatile uint32_t *)(xhci.base + xhci.rt_offset + offset);
}

static void xhci_ring_doorbell(uint32_t slot, uint32_t target)
{
    volatile uint32_t *db = (volatile uint32_t *)(xhci.base + xhci.db_offset + (slot * xhci.doorbell_stride));
    *db = target;
    (void)db;
}

static uint32_t xhci_read_cap32(uint32_t offset)
{
    return *(volatile uint32_t *)(xhci.base + offset);
}

static void xhci_write64_op(uint32_t offset, uint64_t value)
{
    xhci_op(offset)[0] = (uint32_t)value;
    xhci_op(offset)[1] = (uint32_t)(value >> 32);
}

static void xhci_write64_rt(uint32_t offset, uint64_t value)
{
    xhci_rt(offset)[0] = (uint32_t)value;
    xhci_rt(offset)[1] = (uint32_t)(value >> 32);
}

static xhci_trb_t *xhci_cmd_trb(uint32_t index)
{
    return &((xhci_trb_t *)cmd_ring_buf.virt)[index % XHCI_CMD_RING_TRBS];
}

static xhci_trb_t *xhci_evt_trb(uint32_t index)
{
    return &((xhci_trb_t *)evt_ring_buf.virt)[index % XHCI_EVT_RING_TRBS];
}

static void xhci_kick_cmd(void)
{
    xhci_write64_op(0x18U, cmd_ring_buf.phys | 1U);
}

static int xhci_wait_event(uint8_t expected_type, uint32_t *slot_out)
{
    uint32_t timeout = XHCI_TIMEOUT;

    while (timeout-- > 0) {
        xhci_trb_t *trb = xhci_evt_trb(evt_ring_index);
        uint32_t control = trb->control;
        uint32_t type = (control >> 10) & 0x3FU;
        uint32_t cycle = control & 1U;

        if (cycle != evt_ring_cycle) {
            continue;
        }

        if (type == expected_type) {
            if (slot_out != NULL) {
                *slot_out = (uint32_t)(trb->status >> 24);
            }
            evt_ring_index = (evt_ring_index + 1U) % XHCI_EVT_RING_TRBS;
            if (evt_ring_index == 0) {
                evt_ring_cycle ^= 1U;
            }
            xhci_write64_rt(0x38U, evt_ring_buf.phys + ((uint64_t)evt_ring_index * 16U) | (1ULL << 3));
            return 1;
        }

        if (type == XHCI_TRB_TYPE_XFER_EVT || type == XHCI_TRB_TYPE_CMD_CMPL) {
            evt_ring_index = (evt_ring_index + 1U) % XHCI_EVT_RING_TRBS;
            if (evt_ring_index == 0) {
                evt_ring_cycle ^= 1U;
            }
            xhci_write64_rt(0x38U, evt_ring_buf.phys + ((uint64_t)evt_ring_index * 16U) | (1ULL << 3));
        }
    }
    return 0;
}

static int xhci_issue_command(uint32_t trb_type, uint64_t param, uint32_t control_field, uint32_t *slot_out)
{
    xhci_trb_t *trb = xhci_cmd_trb(cmd_ring_index);

    trb->parameter = (uint32_t)param;
    trb->status = (uint32_t)(param >> 32);
    trb->control = (trb_type << 10) | control_field | (1U << 0);
    trb->rsv = 0;

    cmd_ring_index = (cmd_ring_index + 1U) % XHCI_CMD_RING_TRBS;
    xhci_kick_cmd();
    return xhci_wait_event(XHCI_TRB_TYPE_CMD_CMPL, slot_out);
}

static int xhci_reset_controller(void)
{
    uint32_t timeout = XHCI_TIMEOUT;
    uint32_t cmd;

    cmd = xhci_op(0x00U)[0];
    cmd |= XHCI_USBCMD_HCRST;
    xhci_op(0x00U)[0] = cmd;

    while (timeout-- > 0) {
        if ((xhci_op(0x00U)[0] & XHCI_USBCMD_HCRST) == 0) {
            return 1;
        }
    }
    return 0;
}

static int xhci_start_controller(void)
{
    uint32_t cmd;

    xhci_write64_op(0x30U, dcbaa.phys);
    xhci_op(0x38U)[0] = xhci.max_slots;

    xhci_write64_rt(0x20U, erst_buf.phys);
    xhci_rt(0x28U)[0] = 1U;
    xhci_write64_rt(0x30U, evt_ring_buf.phys);
    xhci_write64_rt(0x38U, evt_ring_buf.phys | (1ULL << 3));

    cmd = xhci_op(0x00U)[0];
    cmd |= XHCI_USBCMD_RUN;
    xhci_op(0x00U)[0] = cmd;

    return 1;
}

static int xhci_port_reset(uint32_t port_index)
{
    volatile uint32_t *portsc = &xhci_op(0x400U + (port_index * 0x10U))[0];
    uint32_t timeout = XHCI_TIMEOUT;
    uint32_t value;

    value = *portsc;
    value |= XHCI_PORTSC_PP;
    value |= XHCI_PORTSC_PR;
    *portsc = value;

    while (timeout-- > 0) {
        if ((*portsc & XHCI_PORTSC_PR) == 0) {
            break;
        }
    }

    timeout = XHCI_TIMEOUT;
    while (timeout-- > 0) {
        if ((*portsc & XHCI_PORTSC_PED) != 0) {
            return 1;
        }
    }
    return 0;
}

static void xhci_slot_context_init(void *context, uint32_t port_id)
{
    uint32_t *icc = (uint32_t *)context;
    uint32_t *slot = (uint32_t *)((uint8_t *)context + 32U);

    icc[0] = 1U;
    slot[0] = 0;
    slot[1] = (port_id << 24);
    slot[3] = (8U << 10) | (1U << 27);
}

static int xhci_enable_slot(uint32_t *slot_out)
{
    uint32_t slot = 0;
    if (!xhci_issue_command(XHCI_TRB_TYPE_EN_SLOT, 0, 0, &slot)) {
        return 0;
    }
    if (slot_out != NULL) {
        *slot_out = slot;
    }
    return slot != 0;
}

static int xhci_address_device(uint32_t slot, uint32_t port_id)
{
    dma_buffer_t input_ctx;
    uint64_t *dcbaa_entry;
    uint64_t output_phys;
    void *output_virt;

    if (!dma_alloc_pages(1, &input_ctx)) {
        return 0;
    }
    if (!dma_alloc_page(&output_phys, &output_virt)) {
        return 0;
    }
    memset(input_ctx.virt, 0, 4096);
    memset(output_virt, 0, XHCI_CONTEXT_SIZE);

    dcbaa_entry = (uint64_t *)dcbaa.virt;
    dcbaa_entry[slot] = output_phys;

    xhci_slot_context_init(input_ctx.virt, port_id);

    if (!xhci_issue_command(XHCI_TRB_TYPE_ADDR_DEV, input_ctx.phys, slot << 24, NULL)) {
        pmm_free_frame(output_phys);
        return 0;
    }

    pmm_free_frame(output_phys);
    return 1;
}

static int xhci_control_transfer(
    uint32_t slot,
    uint8_t request_type,
    uint8_t request,
    uint16_t value,
    uint16_t index,
    void *data,
    uint16_t length
)
{
    dma_buffer_t ep_ring;
    uint8_t *setup = (uint8_t *)scratch_buf.virt;
    uint32_t trb_index = 0;
    xhci_trb_t *trb;
    uint32_t timeout;

    if (!dma_alloc_pages(1, &ep_ring)) {
        return 0;
    }

    memset(ep_ring.virt, 0, 4096);
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (uint8_t)(value & 0xFFU);
    setup[3] = (uint8_t)(value >> 8);
    setup[4] = (uint8_t)(index & 0xFFU);
    setup[5] = (uint8_t)(index >> 8);
    setup[6] = (uint8_t)(length & 0xFFU);
    setup[7] = (uint8_t)(length >> 8);

    trb = &((xhci_trb_t *)ep_ring.virt)[trb_index++];
    trb->parameter = scratch_buf.phys;
    trb->status = 8U;
    trb->control = (XHCI_TRB_TYPE_SETUP << 10) | (1U << 5) | (1U << 0);

    if (length > 0) {
        trb = &((xhci_trb_t *)ep_ring.virt)[trb_index++];
        trb->parameter = (uint32_t)virt_to_phys(data);
        trb->status = length;
        trb->control = (XHCI_TRB_TYPE_DATA << 10) |
                       ((request_type & 0x80U) ? (1U << 16) : 0U) |
                       (1U << 5) | (1U << 0);
    }

    trb = &((xhci_trb_t *)ep_ring.virt)[trb_index++];
    trb->parameter = 0;
    trb->status = 0;
    trb->control = (XHCI_TRB_TYPE_STATUS << 10) |
                   ((request_type & 0x80U) ? 0U : (1U << 16)) |
                   (1U << 5) | (1U << 0);

    xhci_ring_doorbell(slot, 0U);
    (void)ep_ring;

    timeout = XHCI_TIMEOUT;
    while (timeout-- > 0) {
        if (xhci_wait_event(XHCI_TRB_TYPE_XFER_EVT, NULL)) {
            return 1;
        }
    }
    return 0;
}

static int xhci_enumerate_port(uint32_t port_index)
{
    volatile uint32_t *portsc = &xhci_op(0x400U + (port_index * 0x10U))[0];
    uint32_t slot;
    usb_device_descriptor_t dev_desc;

    if ((*portsc & XHCI_PORTSC_CCS) == 0) {
        return 0;
    }

    klog_uint(KLOG_INFO, "XHCI", "Port connected = ", port_index + 1U);

    if (!xhci_port_reset(port_index)) {
        klog(KLOG_WARN, "XHCI", "Port reset failed");
        return 0;
    }

    if (!xhci_enable_slot(&slot)) {
        klog(KLOG_WARN, "XHCI", "Enable slot failed");
        return 0;
    }

    klog_uint(KLOG_INFO, "XHCI", "Slot enabled = ", slot);

    if (!xhci_address_device(slot, port_index + 1U)) {
        klog(KLOG_WARN, "XHCI", "Address device failed");
        return 0;
    }

    memset(&dev_desc, 0, sizeof(dev_desc));
    if (!xhci_control_transfer(
            slot,
            0x80U,
            0x06U,
            0x0100U,
            0,
            &dev_desc,
            sizeof(dev_desc))) {
        klog(KLOG_WARN, "XHCI", "GET_DESCRIPTOR failed");
        return 0;
    }

    klog_uint(KLOG_INFO, "XHCI", "USB device class = ", dev_desc.device_class);
    hid_register_device(slot, &dev_desc);
    return 1;
}

void xhci_initialize(void)
{
    device_t *dev;
    pci_bar_info_t bar;
    void *mapped;
    uint32_t hcsparams1;
    uint32_t hccparams1;

    xhci_ready = 0;
    cmd_ring_index = 0;
    evt_ring_index = 0;
    evt_ring_cycle = 1;

    dev = pci_find_by_class(0x0CU, 0x03U, 0x30U);
    if (dev == NULL) {
        klog(KLOG_WARN, "XHCI", "No controller found");
        return;
    }

    klog(KLOG_INFO, "XHCI", "Controller found");

    if (!pci_decode_bar(dev, 0, &bar) || bar.is_io || bar.phys_addr == 0) {
        klog(KLOG_ERROR, "XHCI", "BAR decode failed");
        return;
    }

    pci_enable_bus_mastering(dev);
    mapped = map_mmio(bar.phys_addr, bar.size);
    if (mapped == NULL) {
        klog(KLOG_ERROR, "XHCI", "MMIO map failed");
        return;
    }

    xhci.base = (volatile uint8_t *)mapped;
    xhci.cap_length = xhci.base[0];
    hcsparams1 = xhci_read_cap32(0x04U);
    hccparams1 = xhci_read_cap32(0x10U);
    xhci.max_ports = (hcsparams1 >> 24) & 0xFFU;
    xhci.max_slots = hcsparams1 & 0xFFU;
    if (xhci.max_slots > XHCI_MAX_SLOTS) {
        xhci.max_slots = XHCI_MAX_SLOTS;
    }
    xhci.db_offset = xhci_read_cap32(0x14U) & ~0x1FU;
    xhci.rt_offset = xhci_read_cap32(0x18U) & ~0x1FU;
    xhci.doorbell_stride = (uint32_t)(4U << ((hccparams1 >> 2) & 0x03U));

    if (!xhci_reset_controller()) {
        klog(KLOG_ERROR, "XHCI", "Reset failed");
        return;
    }

    if (!dma_alloc_pages(1, &dcbaa) ||
        !dma_alloc_pages(1, &cmd_ring_buf) ||
        !dma_alloc_pages(1, &evt_ring_buf) ||
        !dma_alloc_pages(1, &erst_buf) ||
        !dma_alloc_pages(1, &scratch_buf)) {
        klog(KLOG_ERROR, "XHCI", "DMA allocation failed");
        return;
    }

    memset(dcbaa.virt, 0, 4096);
    memset(cmd_ring_buf.virt, 0, 4096);
    memset(evt_ring_buf.virt, 0, 4096);

    {
        xhci_erst_entry_t *erst = (xhci_erst_entry_t *)erst_buf.virt;
        erst[0].ring_phys = evt_ring_buf.phys;
        erst[0].ring_size = XHCI_EVT_RING_TRBS;
    }

    if (!xhci_start_controller()) {
        klog(KLOG_ERROR, "XHCI", "Start failed");
        return;
    }

    xhci_ready = 1;
    klog(KLOG_INFO, "XHCI", "Controller running");

    {
        uint32_t port;
        for (port = 0; port < xhci.max_ports; port++) {
            (void)xhci_enumerate_port(port);
        }
    }

    hid_initialize();
}

void xhci_poll(void)
{
    if (!xhci_ready) {
        return;
    }
    hid_poll();
}
