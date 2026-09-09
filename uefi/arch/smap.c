#include "arch/smap.h"
#include "arch/io.h"

/* Set only after CR4.SMAP is successfully enabled in paging_init. */
int smap_enabled;

void user_access_begin(void)
{
    if (smap_enabled) {
        __asm__ volatile("stac" ::: "memory");
    }
}

void user_access_end(void)
{
    if (smap_enabled) {
        __asm__ volatile("clac" ::: "memory");
    }
}
