#include "drivers/nvme.h"
#include "block/block.h"
#include "drivers/pci.h"
#include "drivers/device.h"
#include "mm/dma.h"
#include "platform.h"
#include "lib/string.h"

#define NVME_BAR_INDEX           0U
#define NVME_SECTOR_SIZE         512U
#define NVME_DMA_PAGE_SIZE       4096U
#define NVME_MAX_DMA_SECTORS     (NVME_DMA_PAGE_SIZE / NVME_SECTOR_SIZE)
#define NVME_MIN_SECTORS         2048U
#define NVME_TIMEOUT             1000000U
#define NVME_ADMIN_QSIZE         16U
#define NVME_IO_QSIZE            16U
#define NVME_IO_QID              1U
#define NVME_NS_ID               1U

#define NVME_REG_CAP             0x00U
#define NVME_REG_VS              0x08U
#define NVME_REG_CC              0x14U
#define NVME_REG_CSTS            0x1CU
#define NVME_REG_AQA             0x24U
#define NVME_REG_ASQ             0x28U
#define NVME_REG_ACQ             0x30U

#define NVME_CC_EN               (1U << 0)
#define NVME_CC_CSS_NVM          (0U << 4)
#define NVME_CC_MPS              (0U << 7)
#define NVME_CC_AMS              (0U << 11)
#define NVME_CC_SHN_NONE         (0U << 14)
#define NVME_CC_IOSQES           (6U << 16)
#define NVME_CC_IOCQES           (4U << 20)

#define NVME_CSTS_RDY            (1U << 0)

#define NVME_AQA_ASQS(n)         (((n) - 1U) & 0xFFFU)
#define NVME_AQA_ACQS(n)         ((((n) - 1U) & 0xFFFU) << 16)

#define NVME_SQ_ENTRY_SIZE       64U
#define NVME_CQ_ENTRY_SIZE       16U

#define NVME_ADMIN_CREATE_IO_SQ  0x01U
#define NVME_ADMIN_CREATE_IO_CQ  0x05U
#define NVME_ADMIN_IDENTIFY      0x06U
#define NVME_ADMIN_DELETE_IO_SQ  0x00U
#define NVME_ADMIN_DELETE_IO_CQ  0x04U

#define NVME_CMD_READ            0x02U
#define NVME_CMD_WRITE           0x01U

typedef struct nvme_sq_entry
{
    uint32_t cdw0;
    uint32_t nsid;
    uint64_t rsv;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sq_entry_t;

typedef struct nvme_cq_entry
{
    uint32_t dw0;
    uint32_t rsv;
    uint16_t sqhd;
    uint16_t sqid;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed)) nvme_cq_entry_t;

typedef struct nvme_identify_ns
{
    uint64_t nsze;
    uint64_t ncap;
    uint64_t nuse;
    uint8_t nsfeat;
    uint8_t nlbaf;
    uint8_t flbas;
    uint8_t mc;
    uint8_t dpc;
    uint8_t dps;
    uint8_t nmic;
    uint8_t rescap;
    uint8_t fpi;
    uint8_t dlfeat;
    uint16_t nawun;
    uint16_t nawupf;
    uint16_t nacwu;
    uint16_t nabsn;
    uint16_t nabo;
    uint16_t nabu;
    uint16_t nvmcap[2];
    uint16_t npwg;
    uint16_t npwa;
    uint16_t npdg;
    uint16_t npda;
    uint16_t nows;
    uint16_t mssrl;
    uint32_t mcl;
    uint8_t mssrc;
    uint8_t rsv1[11];
    uint8_t nguid[16];
    uint64_t eui64;
    uint8_t lbaf[16][4];
    uint8_t rsv2[192];
    uint8_t vs[3712];
} __attribute__((packed)) nvme_identify_ns_t;

typedef struct nvme_controller
{
    volatile uint32_t *mmio;
    uint32_t doorbell_stride;
    uint32_t max_transfer;

    dma_buffer_t admin_sq;
    dma_buffer_t admin_cq;
    dma_buffer_t io_sq;
    dma_buffer_t io_cq;
    dma_buffer_t identify;

    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    uint16_t admin_cq_phase;

    uint16_t io_sq_id;
    uint16_t io_cq_id;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    uint16_t io_cq_phase;
    uint16_t io_cmd_id;

    uint32_t block_size;
    uint64_t sector_count;
    block_device_t block;
    int ready;
} nvme_controller_t;

static nvme_controller_t nvme_ctrl;

static uint32_t nvme_read32(uint32_t offset)
{
    if (nvme_ctrl.mmio == NULL) {
        return 0;
    }
    return nvme_ctrl.mmio[offset / 4U];
}

static void nvme_write32(uint32_t offset, uint32_t value)
{
    if (nvme_ctrl.mmio == NULL) {
        return;
    }
    nvme_ctrl.mmio[offset / 4U] = value;
}

static void nvme_write64(uint32_t offset, uint64_t value)
{
    nvme_write32(offset, (uint32_t)value);
    nvme_write32(offset + 4U, (uint32_t)(value >> 32));
}

static uint32_t nvme_cap_doorbell_stride(uint64_t cap)
{
    return (uint32_t)(4U << ((cap >> 32) & 0xFU));
}

static uint32_t nvme_cap_mpsmin(uint64_t cap)
{
    return (uint32_t)(4096U << ((cap >> 48) & 0xFU));
}

static volatile uint32_t *nvme_sq_doorbell(uint16_t qid)
{
    return (volatile uint32_t *)((uint8_t *)nvme_ctrl.mmio + 0x1000U + ((uint32_t)qid * 2U * nvme_ctrl.doorbell_stride));
}

static volatile uint32_t *nvme_cq_doorbell(uint16_t qid)
{
    return (volatile uint32_t *)((uint8_t *)nvme_ctrl.mmio + 0x1000U + ((uint32_t)qid * 2U * nvme_ctrl.doorbell_stride) + nvme_ctrl.doorbell_stride);
}

static int nvme_wait_admin_cqe(uint16_t expected_cid, uint32_t *dw0_out)
{
    nvme_cq_entry_t *cq = (nvme_cq_entry_t *)nvme_ctrl.admin_cq.virt;
    uint32_t timeout = NVME_TIMEOUT;

    while (timeout-- > 0) {
        nvme_cq_entry_t *entry = &cq[nvme_ctrl.admin_cq_head % NVME_ADMIN_QSIZE];
        uint16_t phase = (uint16_t)(entry->status & 1U);

        if (phase == nvme_ctrl.admin_cq_phase) {
            if (entry->cid == expected_cid) {
                if ((entry->status >> 1) != 0) {
                    klog_uint(KLOG_ERROR, "NVME", "Admin SF = ", entry->status);
                    return 0;
                }
                if (dw0_out != NULL) {
                    *dw0_out = entry->dw0;
                }
                nvme_ctrl.admin_cq_head = (uint16_t)((nvme_ctrl.admin_cq_head + 1U) % NVME_ADMIN_QSIZE);
                if (nvme_ctrl.admin_cq_head == 0) {
                    nvme_ctrl.admin_cq_phase ^= 1U;
                }
                *nvme_cq_doorbell(0) = nvme_ctrl.admin_cq_head;
                return 1;
            }
        }
    }
    return 0;
}

static int nvme_submit_admin(
    uint8_t opcode,
    uint32_t nsid,
    uint32_t cdw10,
    uint32_t cdw11,
    uint64_t prp1
)
{
    nvme_sq_entry_t *sq = (nvme_sq_entry_t *)nvme_ctrl.admin_sq.virt;
    uint16_t cid = nvme_ctrl.admin_sq_tail;

    memset(&sq[cid], 0, sizeof(nvme_sq_entry_t));
    sq[cid].cdw0 = ((uint32_t)cid << 16) | opcode;
    sq[cid].nsid = nsid;
    sq[cid].cdw10 = cdw10;
    sq[cid].cdw11 = cdw11;
    sq[cid].prp1 = prp1;

    nvme_ctrl.admin_sq_tail = (uint16_t)((nvme_ctrl.admin_sq_tail + 1U) % NVME_ADMIN_QSIZE);
    *nvme_sq_doorbell(0) = nvme_ctrl.admin_sq_tail;

    return nvme_wait_admin_cqe(cid, NULL);
}

static int nvme_wait_io_cqe(uint16_t expected_cid, uint32_t *dw0_out)
{
    nvme_cq_entry_t *cq = (nvme_cq_entry_t *)nvme_ctrl.io_cq.virt;
    uint32_t timeout = NVME_TIMEOUT;

    while (timeout-- > 0) {
        nvme_cq_entry_t *entry = &cq[nvme_ctrl.io_cq_head % NVME_IO_QSIZE];
        uint16_t phase = (uint16_t)(entry->status & 1U);

        if (phase == nvme_ctrl.io_cq_phase) {
            if (entry->cid == expected_cid) {
                if ((entry->status >> 1) != 0) {
                    return 0;
                }
                if (dw0_out != NULL) {
                    *dw0_out = entry->dw0;
                }
                nvme_ctrl.io_cq_head = (uint16_t)((nvme_ctrl.io_cq_head + 1U) % NVME_IO_QSIZE);
                if (nvme_ctrl.io_cq_head == 0) {
                    nvme_ctrl.io_cq_phase ^= 1U;
                }
                *nvme_cq_doorbell(nvme_ctrl.io_cq_id) = nvme_ctrl.io_cq_head;
                return 1;
            }
        }
    }
    return 0;
}

static int nvme_submit_io(uint8_t opcode, uint64_t lba, uint16_t count, uint64_t prp1, uint16_t *cid_out)
{
    nvme_sq_entry_t *sq = (nvme_sq_entry_t *)nvme_ctrl.io_sq.virt;
    uint16_t cid = nvme_ctrl.io_cmd_id;

    memset(&sq[nvme_ctrl.io_sq_tail], 0, sizeof(nvme_sq_entry_t));
    sq[nvme_ctrl.io_sq_tail].cdw0 = ((uint32_t)cid << 16) | opcode;
    sq[nvme_ctrl.io_sq_tail].nsid = NVME_NS_ID;
    sq[nvme_ctrl.io_sq_tail].prp1 = prp1;
    sq[nvme_ctrl.io_sq_tail].cdw10 = (uint32_t)lba;
    sq[nvme_ctrl.io_sq_tail].cdw11 = (uint32_t)(lba >> 32);
    sq[nvme_ctrl.io_sq_tail].cdw12 = (uint32_t)(count - 1U);

    nvme_ctrl.io_sq_tail = (uint16_t)((nvme_ctrl.io_sq_tail + 1U) % NVME_IO_QSIZE);
    *nvme_sq_doorbell(nvme_ctrl.io_sq_id) = nvme_ctrl.io_sq_tail;

    if (cid_out != NULL) {
        *cid_out = cid;
    }
    nvme_ctrl.io_cmd_id = (uint16_t)((nvme_ctrl.io_cmd_id + 1U) % NVME_IO_QSIZE);
    return nvme_wait_io_cqe(cid, NULL);
}

static int nvme_identify_controller(void)
{
    memset(nvme_ctrl.identify.virt, 0, 4096);
    return nvme_submit_admin(NVME_ADMIN_IDENTIFY, 0, 1, 0, nvme_ctrl.identify.phys);
}

static int nvme_identify_namespace(void)
{
    nvme_identify_ns_t *ns;

    if (!nvme_identify_controller()) {
        klog(KLOG_ERROR, "NVME", "Identify controller failed");
        return 0;
    }

    memset(nvme_ctrl.identify.virt, 0, 4096);
    if (!nvme_submit_admin(NVME_ADMIN_IDENTIFY, NVME_NS_ID, 0, 0, nvme_ctrl.identify.phys)) {
        klog(KLOG_ERROR, "NVME", "Identify namespace failed");
        return 0;
    }

    ns = (nvme_identify_ns_t *)nvme_ctrl.identify.virt;
    nvme_ctrl.sector_count = ns->nsze;
    nvme_ctrl.block_size = NVME_SECTOR_SIZE;

    if (nvme_ctrl.sector_count < NVME_MIN_SECTORS) {
        return 0;
    }
    return 1;
}

static int nvme_create_io_queues(void)
{
    uint32_t cdw10;
    uint32_t cdw11;

    cdw10 = ((NVME_IO_QSIZE - 1U) << 16) | NVME_IO_QID;
    cdw11 = 1U;
    if (!nvme_submit_admin(NVME_ADMIN_CREATE_IO_CQ, 0, cdw10, cdw11, nvme_ctrl.io_cq.phys)) {
        klog(KLOG_ERROR, "NVME", "Create I/O CQ failed");
        return 0;
    }
    nvme_ctrl.io_cq_id = NVME_IO_QID;
    nvme_ctrl.io_cq_head = 0;
    nvme_ctrl.io_cq_phase = 1;
    memset(nvme_ctrl.io_cq.virt, 0, NVME_IO_QSIZE * NVME_CQ_ENTRY_SIZE);

    cdw10 = ((NVME_IO_QSIZE - 1U) << 16) | NVME_IO_QID;
    cdw11 = (NVME_IO_QID << 16) | 1U;
    if (!nvme_submit_admin(NVME_ADMIN_CREATE_IO_SQ, 0, cdw10, cdw11, nvme_ctrl.io_sq.phys)) {
        return 0;
    }
    nvme_ctrl.io_sq_id = NVME_IO_QID;
    nvme_ctrl.io_sq_tail = 0;
    nvme_ctrl.io_cmd_id = 0;
    memset(nvme_ctrl.io_sq.virt, 0, NVME_IO_QSIZE * NVME_SQ_ENTRY_SIZE);
    return 1;
}

static int nvme_read(block_device_t *device, uint32_t lba, uint32_t count, void *buffer)
{
    uint64_t data_phys;
    void *data_virt;
    uint32_t bytes;
    (void)device;

    /* PRP1 only; refuse multi-page transfers until PRP2/list exists. */
    if (!nvme_ctrl.ready || count == 0 ||
        count > NVME_MAX_DMA_SECTORS ||
        (count * nvme_ctrl.block_size) > NVME_DMA_PAGE_SIZE) {
        return 0;
    }

    bytes = count * nvme_ctrl.block_size;
    if (!dma_alloc_page(&data_phys, &data_virt)) {
        return 0;
    }

    if (!nvme_submit_io(NVME_CMD_READ, lba, (uint16_t)count, data_phys, NULL)) {
        pmm_free_frame(data_phys);
        return 0;
    }

    memcpy(buffer, data_virt, bytes);
    pmm_free_frame(data_phys);
    return 1;
}

static int nvme_write(block_device_t *device, uint32_t lba, uint32_t count, const void *buffer)
{
    uint64_t data_phys;
    void *data_virt;
    uint32_t bytes;
    (void)device;

    if (!nvme_ctrl.ready || count == 0 ||
        count > NVME_MAX_DMA_SECTORS ||
        (count * nvme_ctrl.block_size) > NVME_DMA_PAGE_SIZE) {
        return 0;
    }

    bytes = count * nvme_ctrl.block_size;
    if (!dma_alloc_page(&data_phys, &data_virt)) {
        return 0;
    }

    memcpy(data_virt, buffer, bytes);
    if (!nvme_submit_io(NVME_CMD_WRITE, lba, (uint16_t)count, data_phys, NULL)) {
        pmm_free_frame(data_phys);
        return 0;
    }

    pmm_free_frame(data_phys);
    return 1;
}

void nvme_initialize(void)
{
    device_t *dev;
    pci_bar_info_t bar;
    void *mapped;
    uint64_t cap;
    uint32_t timeout;
    uint32_t cc;

    memset(&nvme_ctrl, 0, sizeof(nvme_ctrl));

    dev = pci_find_by_class(0x01U, 0x08U, 0x02U);
    if (dev == NULL) {
        klog(KLOG_WARN, "NVME", "No controller found");
        return;
    }

    klog(KLOG_INFO, "NVME", "Controller found");

    if (!pci_decode_bar(dev, NVME_BAR_INDEX, &bar) || bar.is_io || bar.phys_addr == 0) {
        klog(KLOG_ERROR, "NVME", "BAR decode failed");
        return;
    }

    pci_enable_bus_mastering(dev);
    mapped = map_mmio(bar.phys_addr, bar.size);
    if (mapped == NULL) {
        klog(KLOG_ERROR, "NVME", "MMIO map failed");
        return;
    }

    nvme_ctrl.mmio = (volatile uint32_t *)mapped;
    cap = ((uint64_t)nvme_read32(NVME_REG_CAP + 4U) << 32) | nvme_read32(NVME_REG_CAP);
    nvme_ctrl.doorbell_stride = nvme_cap_doorbell_stride(cap);
    nvme_ctrl.max_transfer = nvme_cap_mpsmin(cap);
    klog_uint(KLOG_INFO, "NVME", "CAP = ", (uint32_t)cap);

    cc = nvme_read32(NVME_REG_CC);
    cc &= ~NVME_CC_EN;
    nvme_write32(NVME_REG_CC, cc);

    timeout = NVME_TIMEOUT;
    while (timeout-- > 0) {
        if ((nvme_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) == 0) {
            break;
        }
    }

    if (!dma_alloc_pages(1, &nvme_ctrl.admin_sq) ||
        !dma_alloc_pages(1, &nvme_ctrl.admin_cq) ||
        !dma_alloc_pages(1, &nvme_ctrl.io_sq) ||
        !dma_alloc_pages(1, &nvme_ctrl.io_cq) ||
        !dma_alloc_pages(1, &nvme_ctrl.identify)) {
        klog(KLOG_ERROR, "NVME", "DMA allocation failed");
        return;
    }

    nvme_ctrl.admin_sq_tail = 0;
    nvme_ctrl.admin_cq_head = 0;
    nvme_ctrl.admin_cq_phase = 1;

    memset(nvme_ctrl.admin_sq.virt, 0, NVME_ADMIN_QSIZE * NVME_SQ_ENTRY_SIZE);
    memset(nvme_ctrl.admin_cq.virt, 0, NVME_ADMIN_QSIZE * NVME_CQ_ENTRY_SIZE);

    nvme_write32(NVME_REG_AQA, NVME_AQA_ASQS(NVME_ADMIN_QSIZE) | NVME_AQA_ACQS(NVME_ADMIN_QSIZE));
    nvme_write64(NVME_REG_ASQ, nvme_ctrl.admin_sq.phys);
    nvme_write64(NVME_REG_ACQ, nvme_ctrl.admin_cq.phys);

    cc = NVME_CC_CSS_NVM | NVME_CC_MPS | NVME_CC_AMS | NVME_CC_SHN_NONE |
         NVME_CC_IOSQES | NVME_CC_IOCQES | NVME_CC_EN;
    nvme_write32(NVME_REG_CC, cc);

    timeout = NVME_TIMEOUT;
    while (timeout-- > 0) {
        if ((nvme_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) != 0) {
            break;
        }
    }
    if ((nvme_read32(NVME_REG_CSTS) & NVME_CSTS_RDY) == 0) {
        klog(KLOG_ERROR, "NVME", "Controller not ready");
        return;
    }

    if (!nvme_identify_namespace()) {
        klog(KLOG_ERROR, "NVME", "Identify namespace failed");
        return;
    }

    if (!nvme_create_io_queues()) {
        klog(KLOG_ERROR, "NVME", "Create I/O queues failed");
        return;
    }

    nvme_ctrl.block.name = "nvme0";
    nvme_ctrl.block.block_size = nvme_ctrl.block_size;
    nvme_ctrl.block.block_count = (uint32_t)nvme_ctrl.sector_count;
    nvme_ctrl.block.read = nvme_read;
    nvme_ctrl.block.write = nvme_write;
    nvme_ctrl.block.priv = &nvme_ctrl;

    if (!block_register(&nvme_ctrl.block)) {
        klog(KLOG_ERROR, "NVME", "Block register failed");
        return;
    }

    nvme_ctrl.ready = 1;
    klog(KLOG_INFO, "NVME", "nvme0 ready");
    klog_uint(KLOG_INFO, "NVME", "Sectors = ", (uint32_t)nvme_ctrl.sector_count);
}
