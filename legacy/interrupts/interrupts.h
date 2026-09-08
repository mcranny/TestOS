#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "types.h"

void interrupts_initialize(void);
void interrupts_enable(void);

/* Install a stub for a legacy PIC IRQ (0-15). Vector = 32+irq. */
void interrupts_register_irq(uint8_t irq, void (*stub)(void));

#endif
