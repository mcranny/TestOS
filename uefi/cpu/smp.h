#ifndef TESTOS_UEFI_CPU_SMP_H
#define TESTOS_UEFI_CPU_SMP_H

#include "limine.h"
#include "types.h"

void smp_init(struct limine_smp_response *response);
void smp_start_aps(void);
void smp_enable_scheduling(void);
uint32_t smp_cpus_online(void);
int smp_scheduling_enabled(void);

#endif
