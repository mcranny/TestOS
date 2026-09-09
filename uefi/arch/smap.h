#ifndef TESTOS_UEFI_ARCH_SMAP_H
#define TESTOS_UEFI_ARCH_SMAP_H

/*
 * SMAP user-access helpers. Implemented in smap.c (not static inline) so every
 * call site shares one definition — STAC/CLAC #UD unless CR4.SMAP is enabled.
 */
extern int smap_enabled;

void user_access_begin(void);
void user_access_end(void);

#endif
