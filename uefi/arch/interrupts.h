#ifndef TESTOS_UEFI_ARCH_INTERRUPTS_H
#define TESTOS_UEFI_ARCH_INTERRUPTS_H

#include "types.h"

void interrupts_init(void);
void interrupts_set_apic_mode(int enabled);
void irq_register(uint8_t irq, void (*handler)(void *));

#endif
