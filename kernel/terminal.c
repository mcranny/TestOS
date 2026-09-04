#include "terminal.h"
#include "memory.h"
#include "port_io.h"
#include "types.h"
#include "multiboot.h"

#define VGA_WIDTH           80
#define VGA_HEIGHT          25
#define VGA_MEMORY          0xB8000
#define SCROLLBACK_LINES    256

#define VGA_CURSOR_INDEX    0x3D4
#define VGA_CURSOR_DATA     0x3D5

static uint16_t *const vga_buffer = (uint16_t *)VGA_MEMORY;

static uint16_t scrollback[SCROLLBACK_LINES][VGA_WIDTH];
static uint32_t line_count;
static uint32_t scroll_offset;
static uint32_t cursor_line;
static uint32_t cursor_col;

static uint8_t terminal_row;
static uint8_t terminal_column;
static uint8_t terminal_color;
static uint8_t *framebuffer;
static uint32_t framebuffer_width;
static uint32_t framebuffer_height;
static uint32_t framebuffer_pitch;
static uint8_t framebuffer_bytes_per_pixel;
static uint8_t framebuffer_red_position;
static uint8_t framebuffer_red_size;
static uint8_t framebuffer_green_position;
static uint8_t framebuffer_green_size;
static uint8_t framebuffer_blue_position;
static uint8_t framebuffer_blue_size;
static uint64_t framebuffer_physical;

static uint32_t framebuffer_component(uint8_t value, uint8_t size, uint8_t position)
{
    uint32_t scaled;

    if (size == 0 || size > 8)
    {
        return 0;
    }

    scaled = ((uint32_t)value * ((1U << size) - 1U)) / 255U;
    return scaled << position;
}

static void framebuffer_pixel(uint32_t x, uint32_t y, uint32_t rgb)
{
    uint32_t pixel;
    uint8_t *destination;
    uint32_t byte;

    if (x >= framebuffer_width || y >= framebuffer_height)
    {
        return;
    }

    pixel = framebuffer_component((uint8_t)(rgb >> 16), framebuffer_red_size, framebuffer_red_position) |
            framebuffer_component((uint8_t)(rgb >> 8), framebuffer_green_size, framebuffer_green_position) |
            framebuffer_component((uint8_t)rgb, framebuffer_blue_size, framebuffer_blue_position);
    destination = framebuffer + y * framebuffer_pitch + x * framebuffer_bytes_per_pixel;
    for (byte = 0; byte < framebuffer_bytes_per_pixel; byte++)
    {
        destination[byte] = (uint8_t)(pixel >> (byte * 8U));
    }
}

static uint8_t boot_glyph_row(char c, uint32_t row)
{
    static const uint8_t glyphs[][7] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* G */
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, /* I */
        {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}, /* J */
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
        {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}  /* Z */
    };
    uint32_t index;

    if (c == ' ' || row >= 7)
    {
        return 0;
    }
    if (c >= 'a' && c <= 'z')
    {
        c = (char)(c - ('a' - 'A'));
    }
    if (c < 'A' || c > 'Z')
    {
        return 0;
    }
    index = (uint32_t)(c - 'A') + 1U;
    return glyphs[index][row];
}

static void framebuffer_stage_draw(const char *stage)
{
    uint32_t x;
    uint32_t y;
    uint32_t letter;
    uint32_t background = 0x000000FFU;
    const uint32_t foreground = 0x00E0F4FFU;
    const uint32_t scale = 8U;

    if (framebuffer == NULL)
    {
        return;
    }
    if (stage[0] == 'G')
    {
        background = 0x00FF00FFU;
    }
    else if (stage[0] == 'M')
    {
        background = 0x0000FFFFU;
    }
    else if (stage[0] == 'P')
    {
        background = 0x00FFFF00U;
    }
    else if (stage[0] == 'I')
    {
        background = 0x00FF8000U;
    }
    else if (stage[0] == 'T')
    {
        background = 0x0000C000U;
    }
    for (y = 0; y < framebuffer_height; y++)
    {
        for (x = 0; x < framebuffer_width; x++)
        {
            framebuffer_pixel(x, y, background);
        }
    }
    for (letter = 0; stage[letter] != '\0'; letter++)
    {
        uint32_t row;
        for (row = 0; row < 7U; row++)
        {
            uint8_t bits = boot_glyph_row(stage[letter], row);
            uint32_t col;
            for (col = 0; col < 5U; col++)
            {
                if ((bits & (1U << (4U - col))) != 0)
                {
                    uint32_t py;
                    uint32_t px;
                    for (py = 0; py < scale; py++)
                    {
                        for (px = 0; px < scale; px++)
                        {
                            uint32_t draw_x = 32U + letter * 6U * scale + col * scale + px;
                            uint32_t draw_y = 32U + row * scale + py;
                            if (draw_x < framebuffer_width && draw_y < framebuffer_height)
                            {
                                framebuffer_pixel(draw_x, draw_y, foreground);
                            }
                        }
                    }
                }
            }
        }
    }
}

static uint16_t terminal_blank_cell(void)
{
    return ((uint16_t)terminal_color << 8) | ' ';
}

static void terminal_enable_cursor(uint8_t cursor_start, uint8_t cursor_end)
{
    outb(VGA_CURSOR_INDEX, 0x0A);
    outb(VGA_CURSOR_DATA, cursor_start);
    outb(VGA_CURSOR_INDEX, 0x0B);
    outb(VGA_CURSOR_DATA, cursor_end);
}

static void terminal_hide_cursor(void)
{
    outb(VGA_CURSOR_INDEX, 0x0A);
    outb(VGA_CURSOR_DATA, 0x20);
    outb(VGA_CURSOR_INDEX, 0x0B);
    outb(VGA_CURSOR_DATA, 0x00);
}

static void terminal_update_cursor(void)
{
    uint16_t position = terminal_row * VGA_WIDTH + terminal_column;

    outb(VGA_CURSOR_INDEX, 0x0F);
    outb(VGA_CURSOR_DATA, (uint8_t)(position & 0xFF));
    outb(VGA_CURSOR_INDEX, 0x0E);
    outb(VGA_CURSOR_DATA, (uint8_t)((position >> 8) & 0xFF));
}

static void terminal_clear_line(uint32_t line)
{
    uint32_t column;
    uint16_t blank = terminal_blank_cell();

    if (line >= SCROLLBACK_LINES)
    {
        return;
    }

    for (column = 0; column < VGA_WIDTH; column++)
    {
        scrollback[line][column] = blank;
    }
}

static void terminal_scrollback_drop_oldest(void)
{
    uint32_t line;
    uint32_t column;

    for (line = 1; line < SCROLLBACK_LINES; line++)
    {
        for (column = 0; column < VGA_WIDTH; column++)
        {
            scrollback[line - 1][column] = scrollback[line][column];
        }
    }

    terminal_clear_line(SCROLLBACK_LINES - 1);

    if (cursor_line > 0)
    {
        cursor_line--;
    }

    if (line_count > 0)
    {
        line_count--;
    }

    if (scroll_offset > 0)
    {
        scroll_offset--;
    }
}

static uint32_t terminal_max_scroll_offset(void)
{
    if (line_count > VGA_HEIGHT)
    {
        return line_count - VGA_HEIGHT;
    }

    return 0;
}

static uint32_t terminal_visible_start_line(void)
{
    if (line_count > VGA_HEIGHT)
    {
        return line_count - VGA_HEIGHT - scroll_offset;
    }

    return 0;
}

static void terminal_sync_cell_to_vga(uint32_t line, uint32_t column)
{
    uint32_t start_line;
    uint32_t row;

    if (scroll_offset != 0 || line >= line_count)
    {
        return;
    }

    start_line = terminal_visible_start_line();

    if (line < start_line)
    {
        return;
    }

    row = line - start_line;

    if (row >= VGA_HEIGHT)
    {
        return;
    }

    vga_buffer[row * VGA_WIDTH + column] = scrollback[line][column];
}

static void terminal_redraw(void)
{
    uint32_t start_line = terminal_visible_start_line();
    uint32_t row;
    uint32_t column;
    uint16_t blank = terminal_blank_cell();

    if (scroll_offset > terminal_max_scroll_offset())
    {
        scroll_offset = terminal_max_scroll_offset();
        start_line = terminal_visible_start_line();
    }

    for (row = 0; row < VGA_HEIGHT; row++)
    {
        uint32_t source_line = start_line + row;

        for (column = 0; column < VGA_WIDTH; column++)
        {
            if (source_line < line_count)
            {
                vga_buffer[row * VGA_WIDTH + column] =
                    scrollback[source_line][column];
            }
            else
            {
                vga_buffer[row * VGA_WIDTH + column] = blank;
            }
        }
    }

    if (scroll_offset == 0)
    {
        terminal_row = cursor_line - start_line;
        terminal_column = (uint8_t)cursor_col;
        terminal_enable_cursor(0, 15);
        terminal_update_cursor();
    }
    else
    {
        terminal_hide_cursor();
    }
}

static void terminal_prepare_new_line(void)
{
    if (cursor_line + 1 >= SCROLLBACK_LINES)
    {
        terminal_scrollback_drop_oldest();
    }

    cursor_line++;
    cursor_col = 0;

    if (cursor_line >= line_count)
    {
        line_count = cursor_line + 1;
    }

    terminal_clear_line(cursor_line);
}

static void terminal_newline(void)
{
    terminal_prepare_new_line();

    if (scroll_offset == 0)
    {
        terminal_redraw();
    }
}

void terminal_initialize(void)
{
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = 0x07;
    line_count = 1;
    scroll_offset = 0;
    cursor_line = 0;
    cursor_col = 0;

    terminal_clear_line(0);
    terminal_redraw();
    terminal_enable_cursor(0, 15);
    terminal_update_cursor();
}

void terminal_initialize_multiboot(uint32_t magic, const multiboot_info_t *info)
{
    terminal_initialize();

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC || info == NULL ||
        (info->flags & MULTIBOOT_INFO_FRAMEBUFFER) == 0 ||
        info->framebuffer_type != 1 || info->framebuffer_bpp < 15 || info->framebuffer_bpp > 32 ||
        info->framebuffer_addr == 0 || info->framebuffer_addr > 0xFFFFFFFFULL ||
        info->framebuffer_width == 0 || info->framebuffer_height == 0 ||
        info->framebuffer_pitch < info->framebuffer_width * ((info->framebuffer_bpp + 7U) / 8U))
    {
        return;
    }

    framebuffer_physical = info->framebuffer_addr;
    framebuffer = (uint8_t *)(uintptr_t)framebuffer_physical;
    framebuffer_width = info->framebuffer_width;
    framebuffer_height = info->framebuffer_height;
    framebuffer_pitch = info->framebuffer_pitch;
    framebuffer_bytes_per_pixel = (uint8_t)((info->framebuffer_bpp + 7U) / 8U);
    framebuffer_red_position = info->framebuffer_red_field_position;
    framebuffer_red_size = info->framebuffer_red_mask_size;
    framebuffer_green_position = info->framebuffer_green_field_position;
    framebuffer_green_size = info->framebuffer_green_mask_size;
    framebuffer_blue_position = info->framebuffer_blue_field_position;
    framebuffer_blue_size = info->framebuffer_blue_mask_size;
    framebuffer_stage_draw("KERNEL");
}

void terminal_set_boot_stage(const char *stage)
{
    if (stage != NULL)
    {
        framebuffer_stage_draw(stage);
    }
}

uintptr_t terminal_framebuffer_address(void)
{
    return (uintptr_t)framebuffer_physical;
}

size_t terminal_framebuffer_size(void)
{
    if (framebuffer == NULL || framebuffer_pitch == 0 || framebuffer_height == 0)
    {
        return 0;
    }
    return (size_t)framebuffer_pitch * framebuffer_height;
}

int terminal_framebuffer_active(void)
{
    return framebuffer != NULL;
}

void terminal_clear(void)
{
    uint32_t line;

    line_count = 1;
    scroll_offset = 0;
    cursor_line = 0;
    cursor_col = 0;
    terminal_row = 0;
    terminal_column = 0;

    for (line = 0; line < SCROLLBACK_LINES; line++)
    {
        terminal_clear_line(line);
    }

    terminal_redraw();
}

void terminal_putchar(char c)
{
    if (c == '\b')
    {
        terminal_backspace();
        return;
    }

    if (c == '\n')
    {
        terminal_newline();
        return;
    }

    if (c == '\r')
    {
        cursor_col = 0;
        terminal_sync_cell_to_vga(cursor_line, 0);

        if (scroll_offset == 0)
        {
            terminal_row = cursor_line - terminal_visible_start_line();
            terminal_column = 0;
            terminal_update_cursor();
        }

        return;
    }

    if (cursor_col >= VGA_WIDTH)
    {
        terminal_newline();
    }

    scrollback[cursor_line][cursor_col] =
        ((uint16_t)terminal_color << 8) | (uint8_t)c;
    terminal_sync_cell_to_vga(cursor_line, cursor_col);
    cursor_col++;

    if (scroll_offset == 0)
    {
        terminal_row = cursor_line - terminal_visible_start_line();
        terminal_column = (uint8_t)cursor_col;
        terminal_update_cursor();
    }
}

void terminal_backspace(void)
{
    if (cursor_col == 0)
    {
        if (cursor_line == 0)
        {
            return;
        }

        cursor_line--;
        cursor_col = VGA_WIDTH - 1;
    }
    else
    {
        cursor_col--;
    }

    scrollback[cursor_line][cursor_col] = terminal_blank_cell();
    terminal_sync_cell_to_vga(cursor_line, cursor_col);

    if (scroll_offset == 0)
    {
        terminal_row = cursor_line - terminal_visible_start_line();
        terminal_column = (uint8_t)cursor_col;
        terminal_update_cursor();
    }
}

void terminal_cursor_left(void)
{
    if (cursor_col > 0)
    {
        cursor_col--;
    }
    else if (cursor_line > 0)
    {
        cursor_line--;
        cursor_col = VGA_WIDTH - 1;
    }

    if (scroll_offset == 0)
    {
        terminal_row = cursor_line - terminal_visible_start_line();
        terminal_column = (uint8_t)cursor_col;
        terminal_update_cursor();
    }
}

void terminal_cursor_right(void)
{
    if (cursor_col + 1 < VGA_WIDTH)
    {
        cursor_col++;
    }
    else if (cursor_line + 1 < line_count)
    {
        cursor_col = 0;
        cursor_line++;
    }

    if (scroll_offset == 0)
    {
        terminal_row = cursor_line - terminal_visible_start_line();
        terminal_column = (uint8_t)cursor_col;
        terminal_update_cursor();
    }
}

void terminal_set_column(uint8_t column)
{
    if (column > VGA_WIDTH)
    {
        column = VGA_WIDTH;
    }

    cursor_col = column;
    terminal_column = column;

    if (scroll_offset == 0)
    {
        terminal_row = cursor_line - terminal_visible_start_line();
        terminal_update_cursor();
    }
}

void terminal_scroll_up(void)
{
    if (terminal_max_scroll_offset() == 0)
    {
        return;
    }

    if (scroll_offset < terminal_max_scroll_offset())
    {
        scroll_offset++;
    }

    terminal_redraw();
}

void terminal_scroll_down(void)
{
    if (scroll_offset == 0)
    {
        return;
    }

    scroll_offset--;
    terminal_redraw();
}

void terminal_scroll_to_bottom(void)
{
    if (scroll_offset == 0)
    {
        return;
    }

    scroll_offset = 0;
    terminal_redraw();
}

int terminal_is_at_bottom(void)
{
    return scroll_offset == 0;
}

void terminal_write(const char *string)
{
    while (*string != '\0')
    {
        terminal_putchar(*string);
        string++;
    }
}
