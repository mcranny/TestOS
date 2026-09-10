#ifndef TESTOS_UEFI_ARCH_APIC_H
#define TESTOS_UEFI_ARCH_APIC_H

#include "types.h"

#define IPI_VECTOR_RESCHED  0xF0
#define IPI_VECTOR_TLB      0xF1
#define LAPIC_TIMER_VECTOR  0x30

void lapic_init(void);
void lapic_ap_init(void);
uint32_t lapic_id(void);
void lapic_eoi(void);
void lapic_send_ipi(uint32_t dest_lapic_id, uint8_t vector);
void lapic_send_ipi_all_excluding_self(uint8_t vector);
void lapic_timer_init(uint32_t hz);
void lapic_timer_ap_init(uint32_t hz);
uint32_t lapic_timer_hz(void);

#endif
