#ifndef TERMINAL_H
#define TERMINAL_H

#include "types.h"
#include "multiboot.h"

void terminal_initialize(void);
void terminal_initialize_multiboot(uint32_t magic, const multiboot_info_t *info);
void terminal_set_boot_stage(const char *stage);
uintptr_t terminal_framebuffer_address(void);
size_t terminal_framebuffer_size(void);
int terminal_framebuffer_active(void);
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
