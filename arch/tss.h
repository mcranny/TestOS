#ifndef TSS_H
#define TSS_H

#include "types.h"

void tss_initialize(void);
void tss_set_kernel_stack(uint32_t stack_top);

#endif
