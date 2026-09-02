#ifndef E1000_H
#define E1000_H

#include "types.h"

#define E1000_VENDOR_ID  0x8086U
#define E1000_DEVICE_ID  0x100EU

#define E1000_REG_CTRL   0x0000U
#define E1000_REG_STATUS 0x0008U
#define E1000_REG_RCTL   0x0100U
#define E1000_REG_TCTL   0x0400U
#define E1000_REG_TIPG   0x0410U
#define E1000_REG_ICR    0x00C0U
#define E1000_REG_ICS    0x00C8U
#define E1000_REG_IMS    0x00D0U
#define E1000_REG_IMC    0x00D8U
#define E1000_REG_RDBAL  0x2800U
#define E1000_REG_RDBAH  0x2804U
#define E1000_REG_RDLEN  0x2808U
#define E1000_REG_RDH    0x2810U
#define E1000_REG_RDT    0x2818U
#define E1000_REG_TDBAL  0x3800U
#define E1000_REG_TDBAH  0x3804U
#define E1000_REG_TDLEN  0x3808U
#define E1000_REG_TDH    0x3810U
#define E1000_REG_TDT    0x3818U
#define E1000_REG_MTA    0x5200U
#define E1000_REG_RAL0   0x5400U
#define E1000_REG_RAH0   0x5404U

#define E1000_CTRL_RST   (1U << 26)
#define E1000_STATUS_LU  (1U << 1)

#define E1000_CTRL_ASDE  (1U << 5)
#define E1000_CTRL_SLU   (1U << 6)

#define E1000_RCTL_EN    (1U << 1)
#define E1000_RCTL_UPE   (1U << 3)
#define E1000_RCTL_MPE   (1U << 4)
#define E1000_RCTL_BAM   (1U << 15)
#define E1000_RCTL_SECRC (1U << 26)
#define E1000_RCTL_BSIZE_2048 0U

#define E1000_TCTL_EN    (1U << 1)
#define E1000_TCTL_PSP   (1U << 3)

#define E1000_TXD_CMD_EOP  (1U << 0)
#define E1000_TXD_CMD_IFCS (1U << 1)
#define E1000_TXD_CMD_RS   (1U << 3)
#define E1000_TXD_STAT_DD  (1U << 0)

#define E1000_RXD_STAT_DD  (1U << 0)
#define E1000_RXD_STAT_EOP (1U << 1)

#define E1000_RAH_AV       (1U << 31)

/* Interrupt Cause / Mask bits used for Phase 7.2-sub proof. */
#define E1000_ICR_LSC    (1U << 2)

#define E1000_TX_DESC_COUNT  8U
#define E1000_RX_DESC_COUNT  8U
#define E1000_RX_BUFFER_SIZE 2048U

typedef struct
{
    uint8_t bytes[6];
} mac_address_t;

typedef struct
{
    uint64_t buffer_addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct
{
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

uint32_t e1000_read_reg(uint32_t offset);
void e1000_write_reg(uint32_t offset, uint32_t value);

const mac_address_t *e1000_get_mac(void);
void e1000_initialize(void);

int e1000_transmit(const void *data, uint16_t length);
int e1000_poll_rx(void);

void e1000_enable_interrupts(void);
void e1000_disable_interrupts(void);
void e1000_irq_handler(void);

#endif
