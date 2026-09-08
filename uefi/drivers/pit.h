#ifndef TESTOS_UEFI_DRIVERS_PIT_H
#define TESTOS_UEFI_DRIVERS_PIT_H

#include "types.h"

void pit_init(uint32_t hz);
uint64_t timer_ticks(void);
uint64_t timer_hz(void);
void timer_irq_handler(void *frame);

#endif
