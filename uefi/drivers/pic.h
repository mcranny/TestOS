#ifndef TESTOS_UEFI_DRIVERS_PIC_H
#define TESTOS_UEFI_DRIVERS_PIC_H

#include "types.h"

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_mask_all(void);
void pic_unmask(uint8_t irq);
void pic_eoi(uint8_t irq);

#endif
