#ifndef PCI_H
#define PCI_H

#include "types.h"
#include "device.h"

#define PCI_COMMAND              0x04U
#define PCI_COMMAND_IO           (1U << 0)
#define PCI_COMMAND_MEMORY       (1U << 1)
#define PCI_COMMAND_BUS_MASTER   (1U << 2)

#define PCI_BAR0                 0x10U
#define PCI_INTERRUPT_LINE       0x3CU

typedef struct pci_bar_info
{
    uint8_t index;
    int is_io;
    int is_64bit;
    uint32_t phys_addr;
    uint32_t size;
} pci_bar_info_t;

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

void pci_write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
void pci_write16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
void pci_write8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value);

uint32_t pci_read_bar(device_t *device, uint8_t bar_index);
uint32_t pci_bar_size(device_t *device, uint8_t bar_index);
int pci_decode_bar(device_t *device, uint8_t bar_index, pci_bar_info_t *out);
void pci_enable_bus_mastering(device_t *device);

device_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);

void pci_initialize(void);

#endif
