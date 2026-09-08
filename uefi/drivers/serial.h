#ifndef TESTOS_UEFI_DRIVERS_SERIAL_H
#define TESTOS_UEFI_DRIVERS_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *text);
void serial_hex64(uint64_t value);
int serial_has_char(void);
char serial_getc(void);

#endif
