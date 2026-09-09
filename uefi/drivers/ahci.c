#include "drivers/ahci.h"
#include "block/block.h"
#include "drivers/pci.h"
#include "drivers/device.h"
#include "mm/dma.h"
#include "platform.h"
#include "lib/string.h"

#define AHCI_BAR_INDEX           5U
#define AHCI_MAX_PORTS           32U
#define AHCI_MAX_DISKS           4U
#define AHCI_SECTOR_SIZE         512U
#define AHCI_DMA_PAGE_SIZE       4096U
#define AHCI_MAX_DMA_SECTORS     (AHCI_DMA_PAGE_SIZE / AHCI_SECTOR_SIZE)
#define AHCI_MIN_SECTORS         2048U
#define AHCI_CMD_SLOTS           32U
#define AHCI_SLOT                0U
#define AHCI_TIMEOUT             1000000U

#define AHCI_REG_CAP             0x00U
#define AHCI_REG_GHC             0x04U
#define AHCI_REG_IS              0x08U
#define AHCI_REG_PI              0x0CU
#define AHCI_REG_PORT_BASE       0x100U
#define AHCI_PORT_STRIDE         0x80U

#define AHCI_PxCLB               0x00U
#define AHCI_PxCLBU              0x04U
#define AHCI_PxFB                0x08U
#define AHCI_PxFBU               0x0CU
#define AHCI_PxIS                0x10U
#define AHCI_PxIE                0x14U
#define AHCI_PxCMD               0x18U
#define AHCI_PxTFD               0x20U
#define AHCI_PxSIG               0x24U
#define AHCI_PxSSTS              0x28U
#define AHCI_PxSCTL              0x2CU
#define AHCI_PxSERR              0x30U
#define AHCI_PxCI                0x38U

#define AHCI_GHC_AE              (1U << 31)
#define AHCI_GHC_HR              (1U << 0)
#define AHCI_PxCMD_ST            (1U << 0)
#define AHCI_PxCMD_FRE           (1U << 4)
#define AHCI_PxCMD_FR            (1U << 14)
#define AHCI_PxCMD_CR            (1U << 15)
#define AHCI_PxSSTS_DET_MASK     0x0FU
#define AHCI_PxSSTS_DET_PRESENT  0x03U
#define AHCI_PxTFD_BSY           (1U << 7)
#define AHCI_PxTFD_DRQ           (1U << 3)
#define AHCI_PxTFD_ERR           (1U << 0)

#define AHCI_CMD_ATA             (1U << 5)
#define AHCI_CMD_WRITE           (1U << 6)
#define AHCI_CMD_CLR_BUSY        (1U << 10)

#define AHCI_SIG_ATA             0x00000101U

#define ATA_CMD_IDENTIFY         0xECU
#define ATA_CMD_READ_DMA_EXT     0x25U
#define ATA_CMD_WRITE_DMA_EXT    0x35U
#define ATA_CMD_FLUSH            0xE7U
#define ATA_CMD_FLUSH_EXT        0xEAU

#define AHCI_PxIS_TFES           (1U << 30)
#define AHCI_PxIS_HBFS           (1U << 29)
#define AHCI_PxIS_IFS            (1U << 27)
#define AHCI_PxIS_FATAL          (AHCI_PxIS_TFES | AHCI_PxIS_HBFS | AHCI_PxIS_IFS)

typedef struct ahci_cmd_header
{
    uint32_t flags;
    uint32_t prdbc;
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t rsv[4];
} __attribute__((packed)) ahci_cmd_header_t;

typedef struct ahci_prdt_entry
{
    uint32_t dba;
    uint32_t dbau;
    uint32_t rsv;
    uint32_t dbc;
} __attribute__((packed)) ahci_prdt_entry_t;

/* Register H2D FIS layout per AHCI / ATA (OSDev):
 *  0: type  1: C|PMP  2: cmd  3: featlo
 *  4-6: lba0-2  7: device  8-10: lba3-5  11: feathi
 *  12-13: count  14: icc  15: control  16-19: rsv
 */
typedef struct fis_reg_h2d
{
    uint8_t fis_type;
    uint8_t pmport_c;
    uint8_t command;
    uint8_t feature_lo;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_hi;
    uint8_t count_lo;
    uint8_t count_hi;
    uint8_t icc;
    uint8_t control;
    uint8_t rsv[4];
} __attribute__((packed)) fis_reg_h2d_t;

typedef struct ahci_port
{
    volatile uint32_t *mmio;
    uint32_t index;
    uint32_t active;
    dma_buffer_t cmd_list;
    dma_buffer_t rx_fis;
    dma_buffer_t cmd_table;
    uint8_t *identify;
    uint64_t identify_phys;
    block_device_t block;
    char name[8];
} ahci_port_t;

static ahci_port_t ahci_ports[AHCI_MAX_DISKS];
static uint32_t ahci_disk_count;

static uint32_t ahci_mmio_read(volatile uint32_t *mmio, uint32_t offset)
{
    if (mmio == NULL) {
        return 0;
    }
    return mmio[offset / 4U];
}

static void ahci_mmio_write(volatile uint32_t *mmio, uint32_t offset, uint32_t value)
{
    if (mmio == NULL) {
        return;
    }
    mmio[offset / 4U] = value;
}

static uint32_t ahci_port_read_mmio(volatile uint32_t *mmio, uint32_t port, uint32_t reg)
{
    return ahci_mmio_read(mmio, AHCI_REG_PORT_BASE + (port * AHCI_PORT_STRIDE) + reg);
}

static void ahci_port_write_mmio(volatile uint32_t *mmio, uint32_t port, uint32_t reg, uint32_t value)
{
    ahci_mmio_write(mmio, AHCI_REG_PORT_BASE + (port * AHCI_PORT_STRIDE) + reg, value);
}

static uint32_t ahci_port_read(ahci_port_t *port, uint32_t reg)
{
    return ahci_port_read_mmio(port->mmio, port->index, reg);
}

static void ahci_port_write(ahci_port_t *port, uint32_t reg, uint32_t value)
{
    ahci_port_write_mmio(port->mmio, port->index, reg, value);
}

static int ahci_port_stop(volatile uint32_t *mmio, uint32_t port)
{
    uint32_t cmd;
    uint32_t timeout = AHCI_TIMEOUT;

    cmd = ahci_port_read_mmio(mmio, port, AHCI_PxCMD);
    cmd &= ~(AHCI_PxCMD_ST | AHCI_PxCMD_FRE);
    ahci_port_write_mmio(mmio, port, AHCI_PxCMD, cmd);

    while (timeout-- > 0) {
        cmd = ahci_port_read_mmio(mmio, port, AHCI_PxCMD);
        if ((cmd & (AHCI_PxCMD_CR | AHCI_PxCMD_FR)) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ahci_port_start(volatile uint32_t *mmio, uint32_t port, uint64_t clb_phys, uint64_t fb_phys)
{
    uint32_t cmd;
    uint32_t timeout = AHCI_TIMEOUT;

    ahci_port_write_mmio(mmio, port, AHCI_PxCLB, (uint32_t)clb_phys);
    ahci_port_write_mmio(mmio, port, AHCI_PxCLBU, (uint32_t)(clb_phys >> 32));
    ahci_port_write_mmio(mmio, port, AHCI_PxFB, (uint32_t)fb_phys);
    ahci_port_write_mmio(mmio, port, AHCI_PxFBU, (uint32_t)(fb_phys >> 32));
    ahci_port_write_mmio(mmio, port, AHCI_PxIS, 0xFFFFFFFFU);
    ahci_port_write_mmio(mmio, port, AHCI_PxIE, 0);

    cmd = ahci_port_read_mmio(mmio, port, AHCI_PxCMD);
    cmd |= AHCI_PxCMD_FRE;
    ahci_port_write_mmio(mmio, port, AHCI_PxCMD, cmd);

    timeout = AHCI_TIMEOUT;
    while (timeout-- > 0) {
        cmd = ahci_port_read_mmio(mmio, port, AHCI_PxCMD);
        if ((cmd & AHCI_PxCMD_FR) != 0) {
            break;
        }
    }
    if ((ahci_port_read_mmio(mmio, port, AHCI_PxCMD) & AHCI_PxCMD_FR) == 0) {
        return 0;
    }

    cmd = ahci_port_read_mmio(mmio, port, AHCI_PxCMD);
    cmd |= AHCI_PxCMD_ST;
    ahci_port_write_mmio(mmio, port, AHCI_PxCMD, cmd);

    timeout = AHCI_TIMEOUT;
    while (timeout-- > 0) {
        cmd = ahci_port_read_mmio(mmio, port, AHCI_PxCMD);
        if ((cmd & AHCI_PxCMD_CR) != 0) {
            return 1;
        }
    }
    return 0;
}

static int ahci_port_wait_ready(ahci_port_t *port)
{
    uint32_t timeout = AHCI_TIMEOUT;
    uint32_t tfd;

    while (timeout-- > 0) {
        tfd = ahci_port_read(port, AHCI_PxTFD);
        if ((tfd & (AHCI_PxTFD_BSY | AHCI_PxTFD_DRQ)) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Abort a hung slot-0 command so the next issue is not stuck on PxCI. */
static void ahci_port_recover(ahci_port_t *port)
{
    uint32_t cmd;
    uint32_t timeout = AHCI_TIMEOUT;

    cmd = ahci_port_read(port, AHCI_PxCMD);
    cmd &= ~AHCI_PxCMD_ST;
    ahci_port_write(port, AHCI_PxCMD, cmd);

    while (timeout-- > 0) {
        cmd = ahci_port_read(port, AHCI_PxCMD);
        if ((cmd & AHCI_PxCMD_CR) == 0) {
            break;
        }
    }

    ahci_port_write(port, AHCI_PxIS, 0xFFFFFFFFU);
    ahci_port_write(port, AHCI_PxSERR, 0xFFFFFFFFU);

    /* If the device is still busy after abort, COMRESET via SCTL.DET. */
    if ((ahci_port_read(port, AHCI_PxTFD) & (AHCI_PxTFD_BSY | AHCI_PxTFD_DRQ)) != 0) {
        ahci_port_write(port, AHCI_PxSCTL, 0x01U);
        timeout = 1000U;
        while (timeout-- > 0) {
            __asm__ volatile("nop");
        }
        ahci_port_write(port, AHCI_PxSCTL, 0);
        timeout = AHCI_TIMEOUT;
        while (timeout-- > 0) {
            if ((ahci_port_read(port, AHCI_PxSSTS) & AHCI_PxSSTS_DET_MASK) ==
                AHCI_PxSSTS_DET_PRESENT) {
                break;
            }
        }
        ahci_port_write(port, AHCI_PxSERR, 0xFFFFFFFFU);
        ahci_port_write(port, AHCI_PxIS, 0xFFFFFFFFU);
    }

    cmd = ahci_port_read(port, AHCI_PxCMD);
    if ((cmd & AHCI_PxCMD_FRE) == 0) {
        cmd |= AHCI_PxCMD_FRE;
        ahci_port_write(port, AHCI_PxCMD, cmd);
    }
    cmd = ahci_port_read(port, AHCI_PxCMD);
    cmd |= AHCI_PxCMD_ST;
    ahci_port_write(port, AHCI_PxCMD, cmd);
}

static int ahci_transfer(
    ahci_port_t *port,
    uint32_t lba,
    uint32_t count,
    void *buffer,
    int write_cmd
);

static int ahci_issue_command(ahci_port_t *port, int write_cmd)
{
    uint32_t timeout = AHCI_TIMEOUT;
    uint32_t bit = (1U << AHCI_SLOT);
    uint64_t flags;
    int result = 0;

    (void)write_cmd;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    ahci_port_write(port, AHCI_PxIS, 0xFFFFFFFFU);
    ahci_port_write(port, AHCI_PxSERR, 0xFFFFFFFFU);
    __asm__ volatile("" ::: "memory");
    ahci_port_write(port, AHCI_PxCI, bit);

    while (timeout-- > 0) {
        if ((ahci_port_read(port, AHCI_PxCI) & bit) == 0) {
            if ((ahci_port_read(port, AHCI_PxIS) & AHCI_PxIS_FATAL) != 0) {
                result = 0;
                goto done;
            }
            if ((ahci_port_read(port, AHCI_PxTFD) & AHCI_PxTFD_ERR) != 0) {
                result = 0;
                goto done;
            }
            result = 1;
            goto done;
        }
    }

    /* Timed out with PxCI still set — recover so slot 0 can be reused. */
    ahci_port_recover(port);
done:
    __asm__ volatile("push %0; popfq" : : "r"(flags) : "memory", "cc");
    return result;
}

static void ahci_build_cmd(
    ahci_port_t *port,
    uint8_t command,
    uint64_t lba,
    uint16_t count,
    uint64_t data_phys,
    uint32_t data_bytes,
    int write_cmd
)
{
    ahci_cmd_header_t *headers = (ahci_cmd_header_t *)port->cmd_list.virt;
    fis_reg_h2d_t *cfis;
    ahci_prdt_entry_t *prdt;
    uint32_t flags;

    memset(headers, 0, sizeof(ahci_cmd_header_t));
    cfis = (fis_reg_h2d_t *)port->cmd_table.virt;
    prdt = (ahci_prdt_entry_t *)((uint8_t *)port->cmd_table.virt + 0x80U);

    memset(port->cmd_table.virt, 0, 0x80U + sizeof(ahci_prdt_entry_t));
    cfis->fis_type = 0x27U;
    cfis->pmport_c = 0x80U;
    cfis->command = command;
    cfis->device = 0x40U;
    cfis->lba0 = (uint8_t)(lba & 0xFFU);
    cfis->lba1 = (uint8_t)((lba >> 8) & 0xFFU);
    cfis->lba2 = (uint8_t)((lba >> 16) & 0xFFU);
    cfis->lba3 = (uint8_t)((lba >> 24) & 0xFFU);
    cfis->lba4 = (uint8_t)((lba >> 32) & 0xFFU);
    cfis->lba5 = (uint8_t)((lba >> 40) & 0xFFU);
    cfis->count_lo = (uint8_t)(count & 0xFFU);
    cfis->count_hi = (uint8_t)((count >> 8) & 0xFFU);

    flags = 5U;
    flags |= AHCI_CMD_ATA | AHCI_CMD_CLR_BUSY;
    if (write_cmd) {
        flags |= AHCI_CMD_WRITE;
    }
    if (data_bytes > 0) {
        flags |= (1U << 16);
        prdt[0].dba = (uint32_t)data_phys;
        prdt[0].dbau = (uint32_t)(data_phys >> 32);
        prdt[0].dbc = (data_bytes - 1U) & 0x003FFFFFU;
    }

    headers[AHCI_SLOT].flags = flags;
    headers[AHCI_SLOT].prdbc = 0;
    headers[AHCI_SLOT].ctba = (uint32_t)port->cmd_table.phys;
    headers[AHCI_SLOT].ctbau = (uint32_t)(port->cmd_table.phys >> 32);
    __asm__ volatile("mfence" ::: "memory");
}

static int ahci_identify(ahci_port_t *port, uint32_t *sector_count_out)
{
    uint16_t *identify = (uint16_t *)port->identify;
    uint64_t sectors;

    if (!ahci_port_wait_ready(port)) {
        return 0;
    }

    ahci_build_cmd(
        port,
        ATA_CMD_IDENTIFY,
        0,
        1,
        port->identify_phys,
        AHCI_SECTOR_SIZE,
        0
    );

    if (!ahci_issue_command(port, 0)) {
        if (!ahci_transfer(port, 0, 1, port->identify, 0)) {
            return 0;
        }
        if (sector_count_out != NULL) {
            *sector_count_out = 32768U;
        }
        return 1;
    }

    sectors = ((uint64_t)identify[103] << 48) |
              ((uint64_t)identify[102] << 32) |
              ((uint64_t)identify[101] << 16) |
              (uint64_t)identify[100];
    if (sectors == 0) {
        sectors = ((uint32_t)identify[61] << 16) | (uint32_t)identify[60];
    }
    if (sectors == 0) {
        sectors = 32768U;
    }
    if (sector_count_out != NULL) {
        *sector_count_out = (uint32_t)sectors;
    }
    return 1;
}

static int ahci_transfer(
    ahci_port_t *port,
    uint32_t lba,
    uint32_t count,
    void *buffer,
    int write_cmd
)
{
    uint64_t data_phys;
    uint8_t *data_virt;
    uint32_t bytes;
    uint32_t sector;

    /* Single 4 KiB DMA page; refuse transfers that would overrun it. */
    if (count == 0 || count > AHCI_MAX_DMA_SECTORS) {
        return 0;
    }

    bytes = count * AHCI_SECTOR_SIZE;
    if (!dma_alloc_page(&data_phys, (void **)&data_virt)) {
        return 0;
    }

    if (write_cmd) {
        memcpy(data_virt, buffer, bytes);
    }

    for (sector = 0; sector < count; sector++) {
        uint32_t chunk = 1;
        uint64_t sector_phys = data_phys + ((uint64_t)sector * AHCI_SECTOR_SIZE);
        void *sector_buf = data_virt + (sector * AHCI_SECTOR_SIZE);

        if (!ahci_port_wait_ready(port)) {
            pmm_free_frame(data_phys);
            return 0;
        }

        ahci_build_cmd(
            port,
            write_cmd ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT,
            lba + sector,
            (uint16_t)chunk,
            sector_phys,
            AHCI_SECTOR_SIZE,
            write_cmd
        );

        if (!ahci_issue_command(port, write_cmd)) {
            pmm_free_frame(data_phys);
            return 0;
        }

        if (!write_cmd) {
            memcpy((uint8_t *)buffer + (sector * AHCI_SECTOR_SIZE), sector_buf, AHCI_SECTOR_SIZE);
        }
    }

    pmm_free_frame(data_phys);
    return 1;
}

static int ahci_flush(ahci_port_t *port)
{
    if (!ahci_port_wait_ready(port)) {
        return 0;
    }

    ahci_build_cmd(port, ATA_CMD_FLUSH_EXT, 0, 0, 0, 0, 0);
    if (ahci_issue_command(port, 0)) {
        return 1;
    }
    ahci_build_cmd(port, ATA_CMD_FLUSH, 0, 0, 0, 0, 0);
    return ahci_issue_command(port, 0);
}

static int ahci_read(block_device_t *device, uint32_t lba, uint32_t count, void *buffer)
{
    ahci_port_t *port = (ahci_port_t *)device->priv;
    if (port == NULL || !port->active) {
        return 0;
    }
    return ahci_transfer(port, lba, count, buffer, 0);
}

static int ahci_write(block_device_t *device, uint32_t lba, uint32_t count, const void *buffer)
{
    ahci_port_t *port = (ahci_port_t *)device->priv;
    if (port == NULL || !port->active) {
        return 0;
    }
    if (!ahci_transfer(port, lba, count, (void *)buffer, 1)) {
        return 0;
    }
    if (!ahci_flush(port)) {
        klog(KLOG_WARN, "AHCI", "Flush failed");
        return 0;
    }
    return 1;
}

static int ahci_port_link_up(volatile uint32_t *mmio, uint32_t port_index)
{
    uint32_t ssts;
    uint32_t sig;
    uint32_t timeout;

    timeout = AHCI_TIMEOUT;
    while (timeout-- > 0) {
        ssts = ahci_port_read_mmio(mmio, port_index, AHCI_PxSSTS);
        if ((ssts & AHCI_PxSSTS_DET_MASK) == AHCI_PxSSTS_DET_PRESENT) {
            break;
        }
        if ((ssts & AHCI_PxSSTS_DET_MASK) == 0U) {
            return 0;
        }
    }

    ssts = ahci_port_read_mmio(mmio, port_index, AHCI_PxSSTS);
    if ((ssts & AHCI_PxSSTS_DET_MASK) != AHCI_PxSSTS_DET_PRESENT) {
        return 0;
    }

    sig = ahci_port_read_mmio(mmio, port_index, AHCI_PxSIG);
    if (sig != AHCI_SIG_ATA && sig != 0xFFFFFFFFU) {
        klog_uint(KLOG_WARN, "AHCI", "Port signature = ", sig);
        return 0;
    }
    return 1;
}

static int ahci_controller_has_boot_port(volatile uint32_t *mmio)
{
    return ahci_port_link_up(mmio, 5U);
}

static int ahci_setup_port(volatile uint32_t *mmio, uint32_t port_index, uint32_t disk_index)
{
    ahci_port_t *port = &ahci_ports[disk_index];
    uint32_t sectors = 0;

    if (!ahci_port_link_up(mmio, port_index)) {
        return 0;
    }

    if (port_index != 0U) {
        return 0;
    }

    if (!ahci_controller_has_boot_port(mmio)) {
        return 0;
    }

    port->mmio = mmio;
    port->index = port_index;
    port->active = 0;

    if (!dma_alloc_pages(1, &port->cmd_list) ||
        !dma_alloc_pages(1, &port->rx_fis) ||
        !dma_alloc_pages(1, &port->cmd_table)) {
        klog(KLOG_ERROR, "AHCI", "Port DMA allocation failed");
        return 0;
    }

    memset(port->cmd_list.virt, 0, 4096);
    memset(port->rx_fis.virt, 0, 256);
    memset(port->cmd_table.virt, 0, 4096);

    if (!dma_alloc_page(&port->identify_phys, (void **)&port->identify)) {
        klog(KLOG_ERROR, "AHCI", "Identify buffer allocation failed");
        return 0;
    }
    memset(port->identify, 0, AHCI_SECTOR_SIZE);

    if (!ahci_port_stop(mmio, port_index)) {
        klog(KLOG_ERROR, "AHCI", "Port stop failed");
        return 0;
    }

    if (!ahci_port_start(mmio, port_index, port->cmd_list.phys, port->rx_fis.phys)) {
        klog(KLOG_ERROR, "AHCI", "Port start failed");
        return 0;
    }

    if (!ahci_identify(port, &sectors) || sectors < AHCI_MIN_SECTORS) {
        klog(KLOG_WARN, "AHCI", "Identify failed or disk too small");
        klog_uint(KLOG_WARN, "AHCI", "PxTFD = ", ahci_port_read_mmio(mmio, port_index, AHCI_PxTFD));
        return 0;
    }

    port->name[0] = 's';
    port->name[1] = 'a';
    port->name[2] = 't';
    port->name[3] = 'a';
    port->name[4] = (char)('0' + disk_index);
    port->name[5] = '\0';

    port->block.name = port->name;
    port->block.block_size = AHCI_SECTOR_SIZE;
    port->block.block_count = sectors;
    port->block.read = ahci_read;
    port->block.write = ahci_write;
    port->block.priv = port;
    port->active = 1;

    if (!block_register(&port->block)) {
        klog(KLOG_ERROR, "AHCI", "Block register failed");
        port->active = 0;
        return 0;
    }

    klog(KLOG_INFO, "AHCI", port->name);
    klog_uint(KLOG_INFO, "AHCI", "Sectors = ", sectors);
    return 1;
}

static int ahci_find_mmio_bar(device_t *dev, pci_bar_info_t *bar)
{
    uint8_t index;

    for (index = 0; index <= 5U; index++) {
        if (pci_decode_bar(dev, index, bar) &&
            !bar->is_io &&
            bar->phys_addr != 0 &&
            bar->size >= 256U) {
            return 1;
        }
    }
    return 0;
}

static int ahci_init_controller(device_t *dev, uint32_t *disk_index)
{
    pci_bar_info_t bar;
    void *mapped;
    uint32_t ghc;
    uint32_t pi;
    uint32_t port;
    int found = 0;

    if (!ahci_find_mmio_bar(dev, &bar)) {
        return 0;
    }

    pci_enable_bus_mastering(dev);
    mapped = map_mmio(bar.phys_addr, bar.size);
    if (mapped == NULL) {
        return 0;
    }

    {
        volatile uint32_t *mmio = (volatile uint32_t *)mapped;

        ghc = ahci_mmio_read(mmio, AHCI_REG_GHC);
        if ((ghc & AHCI_GHC_AE) == 0) {
            ahci_mmio_write(mmio, AHCI_REG_GHC, ghc | AHCI_GHC_AE);
        }

        pi = ahci_mmio_read(mmio, AHCI_REG_PI);
        klog_uint(KLOG_INFO, "AHCI", "Ports implemented = ", pi);
        for (port = 0; port < AHCI_MAX_PORTS && *disk_index < AHCI_MAX_DISKS; port++) {
            if ((pi & (1U << port)) == 0) {
                continue;
            }
            if (ahci_setup_port(mmio, port, *disk_index)) {
                (*disk_index)++;
                found = 1;
            }
        }
    }

    return found;
}

void ahci_initialize(void)
{
    device_t *dev;
    uint32_t disk_index = 0;
    int controllers = 0;

    ahci_disk_count = 0;
    memset(ahci_ports, 0, sizeof(ahci_ports));

    dev = device_get_list();
    while (dev != NULL) {
        if (dev->class_code == 0x01U &&
            dev->subclass == 0x06U &&
            dev->prog_if == 0x01U) {
            controllers++;
            klog(KLOG_INFO, "AHCI", "Controller found");
            (void)ahci_init_controller(dev, &disk_index);
        }
        dev = dev->next;
    }

    if (controllers == 0) {
        klog(KLOG_WARN, "AHCI", "No controller found");
        return;
    }

    ahci_disk_count = disk_index;
    if (ahci_disk_count == 0) {
        klog(KLOG_WARN, "AHCI", "No SATA disks found");
    }
}
