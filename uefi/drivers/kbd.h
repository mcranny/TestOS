#ifndef TESTOS_UEFI_DRIVERS_KBD_H
#define TESTOS_UEFI_DRIVERS_KBD_H

#include "types.h"

void kbd_init(void);
void kbd_irq_handler(void *frame);
/* Returns 0 if no character ready; otherwise the ASCII character. */
char kbd_getchar(void);

#endif
