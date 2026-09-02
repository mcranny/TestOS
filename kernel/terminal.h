#ifndef TERMINAL_H
#define TERMINAL_H

#include "types.h"

void terminal_initialize(void);
void terminal_clear(void);
void terminal_putchar(char c);
void terminal_write(const char *string);
void terminal_backspace(void);

void terminal_cursor_left(void);
void terminal_cursor_right(void);
void terminal_set_column(uint8_t column);

void terminal_scroll_up(void);
void terminal_scroll_down(void);
void terminal_scroll_to_bottom(void);
int terminal_is_at_bottom(void);

#endif // TERMINAL_H
