#include "drivers/fb.h"

struct display {
    volatile uint8_t *address;
    uint64_t width, height, pitch;
    uint8_t bytes_per_pixel;
    uint8_t red_size, red_shift, green_size, green_shift, blue_size, blue_shift;
};

static struct display screen;

static uint32_t component(uint8_t value, uint8_t size, uint8_t shift)
{
    if (size == 0 || size > 8 || shift + size > 32) return 0;
    return (((uint32_t)value * ((1U << size) - 1U)) / 255U) << shift;
}

static void pixel(uint64_t x, uint64_t y, uint32_t rgb)
{
    uint32_t value;
    volatile uint8_t *dst;
    uint8_t byte;
    if (x >= screen.width || y >= screen.height) return;
    value = component((uint8_t)(rgb >> 16), screen.red_size, screen.red_shift) |
            component((uint8_t)(rgb >> 8), screen.green_size, screen.green_shift) |
            component((uint8_t)rgb, screen.blue_size, screen.blue_shift);
    dst = screen.address + y * screen.pitch + x * screen.bytes_per_pixel;
    for (byte = 0; byte < screen.bytes_per_pixel; byte++) {
        dst[byte] = (uint8_t)(value >> (byte * 8));
    }
}

static uint8_t glyph(char c, uint8_t row)
{
    static const uint8_t font[][7] = {
        {0,0,0,0,0,0,0},{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},
        {31,16,16,30,16,16,16},{14,17,16,23,17,17,14},{17,17,17,31,17,17,17},
        {31,4,4,4,4,4,31},{1,1,1,1,17,17,14},{17,18,20,24,20,18,17},
        {16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},
        {30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},
        {17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    static const uint8_t digits[][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},
        {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
        {14,17,17,15,1,2,12}
    };
    /* Punctuation used by prompts, paths, and help text. */
    static const uint8_t punct_slash[7] = {1,1,2,4,8,16,16};
    static const uint8_t punct_hash[7] = {10,10,31,10,31,10,10};
    static const uint8_t punct_dot[7] = {0,0,0,0,0,12,12};
    static const uint8_t punct_dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t punct_colon[7] = {0,12,12,0,12,12,0};
    static const uint8_t punct_eq[7] = {0,0,31,0,31,0,0};
    static const uint8_t punct_uscore[7] = {0,0,0,0,0,0,31};
    static const uint8_t punct_plus[7] = {0,4,4,31,4,4,0};
    static const uint8_t punct_lt[7] = {2,4,8,16,8,4,2};
    static const uint8_t punct_gt[7] = {8,4,2,1,2,4,8};
    static const uint8_t punct_lpar[7] = {4,8,16,16,16,8,4};
    static const uint8_t punct_rpar[7] = {8,4,2,2,2,4,8};
    static const uint8_t punct_excl[7] = {4,4,4,4,0,0,4};
    static const uint8_t punct_quest[7] = {14,17,1,2,4,0,4};
    static const uint8_t punct_comma[7] = {0,0,0,0,4,4,8};
    static const uint8_t punct_semi[7] = {0,12,12,0,12,4,8};
    static const uint8_t punct_quote[7] = {10,10,10,0,0,0,0};
    static const uint8_t punct_apos[7] = {4,4,4,0,0,0,0};
    static const uint8_t punct_star[7] = {0,10,4,31,4,10,0};
    static const uint8_t punct_at[7] = {14,17,23,21,23,16,14};

    if (row >= 7) return 0;
    if (c == ' ') return 0;
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return font[c - 'A' + 1][row];
    switch (c) {
        case '/': case '\\': return punct_slash[row];
        case '#': return punct_hash[row];
        case '.': return punct_dot[row];
        case '-': return punct_dash[row];
        case ':': return punct_colon[row];
        case '=': return punct_eq[row];
        case '_': return punct_uscore[row];
        case '+': return punct_plus[row];
        case '<': return punct_lt[row];
        case '>': return punct_gt[row];
        case '(': return punct_lpar[row];
        case ')': return punct_rpar[row];
        case '!': return punct_excl[row];
        case '?': return punct_quest[row];
        case ',': return punct_comma[row];
        case ';': return punct_semi[row];
        case '"': return punct_quote[row];
        case '\'': return punct_apos[row];
        case '*': return punct_star[row];
        case '@': return punct_at[row];
        default: return 0;
    }
}

void fb_draw_text(const char *text, uint64_t x, uint64_t y, uint32_t color)
{
    uint64_t letter;
    for (letter = 0; text[letter]; letter++) {
        uint8_t row;
        for (row = 0; row < 7; row++) {
            uint8_t bits = glyph(text[letter], row);
            uint8_t col;
            for (col = 0; col < 5; col++) {
                if (bits & (1U << (4 - col))) {
                    uint8_t dy, dx;
                    for (dy = 0; dy < 8; dy++) {
                        for (dx = 0; dx < 8; dx++) {
                            pixel(x + letter * 48 + col * 8 + dx, y + row * 8 + dy, color);
                        }
                    }
                }
            }
        }
    }
}

void fb_init(struct limine_framebuffer *fb)
{
    screen.address = fb->address;
    screen.width = fb->width;
    screen.height = fb->height;
    screen.pitch = fb->pitch;
    screen.bytes_per_pixel = 4;
    screen.red_size = fb->red_mask_size;
    screen.red_shift = fb->red_mask_shift;
    screen.green_size = fb->green_mask_size;
    screen.green_shift = fb->green_mask_shift;
    screen.blue_size = fb->blue_mask_size;
    screen.blue_shift = fb->blue_mask_shift;
}

int fb_ready(void)
{
    return screen.address != 0;
}

void fb_stage(const char *name)
{
    uint64_t x, y;
    if (!fb_ready()) return;
    for (y = 0; y < screen.height; y++) {
        for (x = 0; x < screen.width; x++) {
            pixel(x, y, 0x0b1d34);
        }
    }
    fb_draw_text("TESTOS", 32, 32, 0xe0f4ff);
    fb_draw_text(name, 32, 112, 0xffffff);
}

#define FB_CELL_W 8
#define FB_CELL_H 12
#define FB_MARGIN_X 0
#define FB_FG 0xd7e6f0
#define FB_BG 0x0b1d34
#define FB_CURSOR 0xf2f7fb
#define FB_BLINK_TICKS 50

static uint64_t con_cols;
static uint64_t con_rows;
static uint64_t con_x;
static uint64_t con_y;
static int cursor_on;
static int cursor_drawn;
static uint64_t cursor_last_toggle;

static void fb_fill_rect(uint64_t x0, uint64_t y0, uint64_t w, uint64_t h, uint32_t rgb)
{
    uint64_t x, y;
    for (y = y0; y < y0 + h && y < screen.height; y++) {
        for (x = x0; x < x0 + w && x < screen.width; x++) {
            pixel(x, y, rgb);
        }
    }
}

static void fb_draw_glyph_small(char c, uint64_t x, uint64_t y, uint32_t color)
{
    uint8_t row;
    for (row = 0; row < 7; row++) {
        uint8_t bits = glyph(c, row);
        uint8_t col;
        for (col = 0; col < 5; col++) {
            if (bits & (1U << (4 - col))) {
                pixel(x + col + 1, y + row + 2, color);
            }
        }
    }
}

static void cursor_erase(void)
{
    if (!fb_ready() || !cursor_drawn) return;
    fb_fill_rect(con_x * FB_CELL_W, con_y * FB_CELL_H, FB_CELL_W, FB_CELL_H, FB_BG);
    cursor_drawn = 0;
}

static void cursor_paint(void)
{
    if (!fb_ready() || !cursor_on) return;
    /* Solid block cursor (PowerShell-style). */
    fb_fill_rect(con_x * FB_CELL_W, con_y * FB_CELL_H, FB_CELL_W, FB_CELL_H, FB_CURSOR);
    cursor_drawn = 1;
}

static void cursor_hide(void)
{
    cursor_erase();
}

static void cursor_show(void)
{
    cursor_on = 1;
    cursor_paint();
}

static void fb_console_scroll(void)
{
    uint64_t row_bytes = screen.pitch;
    uint64_t line_px = FB_CELL_H;
    uint64_t y;
    if (!fb_ready() || con_rows < 2) return;
    cursor_hide();
    for (y = 0; y < (con_rows - 1) * line_px; y++) {
        volatile uint8_t *dst = screen.address + y * row_bytes;
        volatile uint8_t *src = screen.address + (y + line_px) * row_bytes;
        uint64_t x;
        for (x = 0; x < screen.width * screen.bytes_per_pixel; x++) {
            dst[x] = src[x];
        }
    }
    fb_fill_rect(0, (con_rows - 1) * line_px, screen.width, line_px, FB_BG);
    con_y = con_rows - 1;
    con_x = FB_MARGIN_X;
    cursor_show();
}

void fb_console_begin(void)
{
    if (!fb_ready()) return;
    con_cols = screen.width / FB_CELL_W;
    con_rows = screen.height / FB_CELL_H;
    if (con_cols <= FB_MARGIN_X + 1) con_cols = FB_MARGIN_X + 2;
    if (con_rows == 0) con_rows = 1;
    cursor_on = 1;
    cursor_drawn = 0;
    cursor_last_toggle = 0;
    fb_console_clear();
}

void fb_console_clear(void)
{
    uint64_t x, y;
    if (!fb_ready()) return;
    cursor_hide();
    for (y = 0; y < screen.height; y++) {
        for (x = 0; x < screen.width; x++) {
            pixel(x, y, FB_BG);
        }
    }
    con_x = FB_MARGIN_X;
    con_y = 0;
    cursor_on = 1;
    cursor_show();
}

void fb_console_putc(char c)
{
    if (!fb_ready()) return;
    cursor_hide();
    if (c == '\r') {
        con_x = FB_MARGIN_X;
        cursor_show();
        return;
    }
    if (c == '\n') {
        con_x = FB_MARGIN_X;
        con_y++;
        if (con_y >= con_rows) {
            fb_console_scroll();
            return;
        }
        cursor_show();
        return;
    }
    if (c == '\b') {
        if (con_x > FB_MARGIN_X) {
            con_x--;
            fb_fill_rect(con_x * FB_CELL_W, con_y * FB_CELL_H, FB_CELL_W, FB_CELL_H, FB_BG);
        }
        cursor_show();
        return;
    }
    if (c == '\t') c = ' ';
    fb_fill_rect(con_x * FB_CELL_W, con_y * FB_CELL_H, FB_CELL_W, FB_CELL_H, FB_BG);
    fb_draw_glyph_small(c, con_x * FB_CELL_W, con_y * FB_CELL_H, FB_FG);
    con_x++;
    if (con_x >= con_cols) {
        con_x = FB_MARGIN_X;
        con_y++;
        if (con_y >= con_rows) {
            fb_console_scroll();
            return;
        }
    }
    cursor_show();
}

void fb_console_puts(const char *text)
{
    while (*text) {
        fb_console_putc(*text++);
    }
}

void fb_console_tick(uint64_t ticks)
{
    if (!fb_ready()) return;
    if (ticks - cursor_last_toggle < FB_BLINK_TICKS) return;
    cursor_last_toggle = ticks;
    if (cursor_on) {
        if (cursor_drawn) {
            cursor_erase();
        } else {
            cursor_paint();
        }
    }
}
