#ifndef TESTOS_UEFI_DRIVERS_CONSOLE_H
#define TESTOS_UEFI_DRIVERS_CONSOLE_H

#include "types.h"

void console_init(void);
void console_putc(char c);
void console_puts(const char *text);
void console_write_hex64(uint64_t value);
char console_getchar(void);

#endif
