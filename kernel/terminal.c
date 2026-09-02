#include "terminal.h"
#include "memory.h"
#include "port_io.h"
#include "types.h"

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
