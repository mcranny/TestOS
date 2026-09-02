#ifndef PCI_H
#define PCI_H

#include "types.h"

uint32_t pci_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t pci_read8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

void pci_initialize(void);

#endif
