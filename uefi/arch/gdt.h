#ifndef TESTOS_UEFI_ARCH_GDT_H
#define TESTOS_UEFI_ARCH_GDT_H

#include "types.h"

/*
 * GDT layout (chosen to be SYSCALL/SYSRET compatible):
 *   0x00 null
 *   0x08 kernel code   (DPL0, L=1)
 *   0x10 kernel data   (DPL0)
 *   0x18 user data     (DPL3)
 *   0x20 user code     (DPL3, L=1)
 *   0x28 TSS           (16-byte system descriptor, spans 0x28..0x30)
 *
 * SYSRET requires STAR[63:48] such that +16 = user code and +8 = user data,
 * i.e. STAR[63:48] = GDT_KERNEL_DATA (0x10) with this layout — not user data.
 */
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_DATA   0x18
#define GDT_USER_CODE   0x20
#define GDT_TSS         0x28

/* Selectors as loaded at ring 3 (RPL = 3). */
#define USER_CODE_SEL   (GDT_USER_CODE | 3)
#define USER_DATA_SEL   (GDT_USER_DATA | 3)

void gdt_init(void);

#endif
