#ifndef SERIAL_H
#define SERIAL_H

void serial_initialize(void);
void serial_write_char(char c);
void serial_write(const char *str);

#endif
