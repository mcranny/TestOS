#include "drivers/kbd.h"
#include "arch/io.h"

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define KBD_BUF 128

static uint8_t sc_buf[KBD_BUF];
static uint32_t sc_head;
static uint32_t sc_tail;
static char ch_buf[KBD_BUF];
static uint32_t ch_head;
static uint32_t ch_tail;
static int shift;
static int extended;

static void push_sc(uint8_t sc)
{
    uint32_t next = (sc_head + 1) % KBD_BUF;
    if (next == sc_tail) return;
    sc_buf[sc_head] = sc;
    sc_head = next;
}

static int pop_sc(uint8_t *out)
{
    if (sc_head == sc_tail) return 0;
    *out = sc_buf[sc_tail];
    sc_tail = (sc_tail + 1) % KBD_BUF;
    return 1;
}

static void push_ch(char c)
{
    uint32_t next = (ch_head + 1) % KBD_BUF;
    if (next == ch_tail || c == 0) return;
    ch_buf[ch_head] = c;
    ch_head = next;
}

static char map_scancode(uint8_t sc, int sh)
{
    static const char normal[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/'
    };
    static const char shifted[] = {
        0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?'
    };
    if (sc == 0x39) return ' ';
    if (sc >= sizeof(normal)) return 0;
    return sh ? shifted[sc] : normal[sc];
}

static void process_sc(uint8_t sc)
{
    int release;
    uint8_t code;

    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    release = (sc & 0x80) != 0;
    code = (uint8_t)(sc & 0x7F);

    if (extended) {
        extended = 0;
        return;
    }

    if (code == 0x2A || code == 0x36) {
        shift = release ? 0 : 1;
        return;
    }
    if (release) return;
    push_ch(map_scancode(code, shift));
}

void kbd_init(void)
{
    sc_head = sc_tail = 0;
    ch_head = ch_tail = 0;
    shift = 0;
    extended = 0;
    while (inb(KBD_STATUS) & 1) {
        (void)inb(KBD_DATA);
    }
}

void kbd_irq_handler(void *frame)
{
    (void)frame;
    if (inb(KBD_STATUS) & 1) {
        push_sc(inb(KBD_DATA));
    }
}

char kbd_getchar(void)
{
    uint8_t sc;
    while (pop_sc(&sc)) {
        process_sc(sc);
    }
    if (ch_head == ch_tail) return 0;
    sc = (uint8_t)ch_buf[ch_tail];
    ch_tail = (ch_tail + 1) % KBD_BUF;
    return (char)sc;
}
