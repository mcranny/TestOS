#ifndef TESTOS_UEFI_ARCH_IOAPIC_H
#define TESTOS_UEFI_ARCH_IOAPIC_H

#include "types.h"

void ioapic_init(void);
void ioapic_route_isa_irq(uint8_t isa_irq, uint8_t vector, uint32_t dest_lapic_id);
int ioapic_present(void);

#endif
