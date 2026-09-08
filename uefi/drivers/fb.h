#ifndef TESTOS_UEFI_DRIVERS_FB_H
#define TESTOS_UEFI_DRIVERS_FB_H

#include "limine.h"
#include "types.h"

void fb_init(struct limine_framebuffer *fb);
void fb_stage(const char *name);
void fb_draw_text(const char *text, uint64_t x, uint64_t y, uint32_t color);
int fb_ready(void);

/* Small text console for interactive use (serial is mirrored separately). */
void fb_console_begin(void);
void fb_console_clear(void);
void fb_console_putc(char c);
void fb_console_puts(const char *text);
/* Blink PowerShell-style block cursor; pass current timer ticks. */
void fb_console_tick(uint64_t ticks);

#endif
