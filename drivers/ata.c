#include "ata.h"
#include "block.h"
#include "port_io.h"
#include "terminal.h"

#define ATA_PRIMARY_DATA       0x1F0
#define ATA_PRIMARY_ERROR      0x1F1
#define ATA_PRIMARY_SECTOR_CNT 0x1F2
#define ATA_PRIMARY_LBA_LO     0x1F3
#define ATA_PRIMARY_LBA_MID    0x1F4
#define ATA_PRIMARY_LBA_HI     0x1F5
#define ATA_PRIMARY_DRIVE      0x1F6
#define ATA_PRIMARY_STATUS     0x1F7
#define ATA_PRIMARY_COMMAND    0x1F7
#define ATA_PRIMARY_CONTROL    0x3F6

#define ATA_CMD_READ_PIO       0x20
#define ATA_CMD_WRITE_PIO      0x30
#define ATA_CMD_CACHE_FLUSH    0xE7
#define ATA_CMD_IDENTIFY       0xEC

#define ATA_STATUS_ERR         0x01
#define ATA_STATUS_DRQ         0x08
#define ATA_STATUS_DF          0x20
#define ATA_STATUS_BSY         0x80

#define ATA_SECTOR_SIZE        512U

static block_device_t ata_device;
static uint16_t identify_buffer[256];
static int ata_present;

static int ata_wait_ready(void)
{
    uint32_t timeout = 1000000U;
    uint8_t status;

    while (timeout-- > 0)
    {
        status = inb(ATA_PRIMARY_STATUS);

        if ((status & ATA_STATUS_BSY) == 0)
        {
            if ((status & ATA_STATUS_ERR) != 0 || (status & ATA_STATUS_DF) != 0)
            {
                return 0;
            }

            return 1;
        }
    }

    return 0;
}

static int ata_wait_drq(void)
{
    uint32_t timeout = 1000000U;
    uint8_t status;

    while (timeout-- > 0)
    {
        status = inb(ATA_PRIMARY_STATUS);

        if ((status & ATA_STATUS_BSY) != 0)
        {
            continue;
        }

        if ((status & ATA_STATUS_ERR) != 0 || (status & ATA_STATUS_DF) != 0)
        {
            return 0;
        }

        if ((status & ATA_STATUS_DRQ) != 0)
        {
            return 1;
        }
    }

    return 0;
}

static void ata_select_lba(uint32_t lba, uint8_t count)
{
    outb(ATA_PRIMARY_DRIVE, (uint8_t)(0xE0 | ((lba >> 24) & 0x0FU)));
    outb(ATA_PRIMARY_SECTOR_CNT, count);
    outb(ATA_PRIMARY_LBA_LO, (uint8_t)(lba & 0xFFU));
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)((lba >> 8) & 0xFFU));
    outb(ATA_PRIMARY_LBA_HI, (uint8_t)((lba >> 16) & 0xFFU));
}

static int ata_read_sectors(
    block_device_t *device,
    uint32_t lba,
    uint32_t count,
    void *buffer
)
{
    uint16_t *out = (uint16_t *)buffer;
    uint32_t sector;
    uint32_t word;

    (void)device;

    if (!ata_present || count == 0 || count > 255U)
    {
        return 0;
    }

    for (sector = 0; sector < count; sector++)
    {
        if (!ata_wait_ready())
        {
            return 0;
        }

        ata_select_lba(lba + sector, 1);
        outb(ATA_PRIMARY_COMMAND, ATA_CMD_READ_PIO);

        if (!ata_wait_drq())
        {
            return 0;
        }

        for (word = 0; word < (ATA_SECTOR_SIZE / 2U); word++)
        {
            out[sector * (ATA_SECTOR_SIZE / 2U) + word] = inw(ATA_PRIMARY_DATA);
        }
    }

    return 1;
}

static int ata_write_sectors(
    block_device_t *device,
    uint32_t lba,
    uint32_t count,
    const void *buffer
)
{
    const uint16_t *in = (const uint16_t *)buffer;
    uint32_t sector;
    uint32_t word;

    (void)device;

    if (!ata_present || count == 0 || count > 255U)
    {
        return 0;
    }

    for (sector = 0; sector < count; sector++)
    {
        if (!ata_wait_ready())
        {
            return 0;
        }

        ata_select_lba(lba + sector, 1);
        outb(ATA_PRIMARY_COMMAND, ATA_CMD_WRITE_PIO);

        if (!ata_wait_drq())
        {
            return 0;
        }

        for (word = 0; word < (ATA_SECTOR_SIZE / 2U); word++)
        {
            outw(
                ATA_PRIMARY_DATA,
                in[sector * (ATA_SECTOR_SIZE / 2U) + word]
            );
        }

        outb(ATA_PRIMARY_COMMAND, ATA_CMD_CACHE_FLUSH);

        if (!ata_wait_ready())
        {
            return 0;
        }
    }

    return 1;
}

static int ata_identify(uint32_t *sector_count_out)
{
    uint32_t word;
    uint8_t status;
    uint32_t sectors;

    outb(ATA_PRIMARY_DRIVE, 0xA0);
    outb(ATA_PRIMARY_SECTOR_CNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    outb(ATA_PRIMARY_COMMAND, ATA_CMD_IDENTIFY);

    status = inb(ATA_PRIMARY_STATUS);

    if (status == 0)
    {
        return 0;
    }

    if (!ata_wait_ready())
    {
        return 0;
    }

    if (inb(ATA_PRIMARY_LBA_MID) != 0 || inb(ATA_PRIMARY_LBA_HI) != 0)
    {
        return 0;
    }

    if (!ata_wait_drq())
    {
        return 0;
    }

    for (word = 0; word < 256U; word++)
    {
        identify_buffer[word] = inw(ATA_PRIMARY_DATA);
    }

    /* Words 60-61: total user-addressable sectors (LBA28). */
    sectors =
        ((uint32_t)identify_buffer[61] << 16) | (uint32_t)identify_buffer[60];

    if (sectors == 0)
    {
        return 0;
    }

    *sector_count_out = sectors;
    return 1;
}

void ata_initialize(void)
{
    uint32_t sectors = 0;

    ata_present = 0;
    outb(ATA_PRIMARY_CONTROL, 0x02);

    if (!ata_identify(&sectors))
    {
        terminal_write("ATA: no disk\n");
        return;
    }

    ata_device.name = "hd0";
    ata_device.block_size = ATA_SECTOR_SIZE;
    ata_device.block_count = sectors;
    ata_device.read = ata_read_sectors;
    ata_device.write = ata_write_sectors;
    ata_device.priv = NULL;
    ata_present = 1;

    if (!block_register(&ata_device))
    {
        terminal_write("ATA: register failed\n");
        ata_present = 0;
        return;
    }

    terminal_write("ATA: hd0 ready\n");
}
