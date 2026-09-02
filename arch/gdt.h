#ifndef GDT_H
#define GDT_H

#include "types.h"

#define KERNEL_CODE_SEGMENT 0x08
#define KERNEL_DATA_SEGMENT 0x10
#define USER_CODE_SEGMENT   0x18
#define USER_DATA_SEGMENT   0x20
#define TSS_SEGMENT         0x28

#define USER_CODE_SELECTOR  (USER_CODE_SEGMENT | 3)
#define USER_DATA_SELECTOR  (USER_DATA_SEGMENT | 3)

void gdt_initialize(void);

#endif
