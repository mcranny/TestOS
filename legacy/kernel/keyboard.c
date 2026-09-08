#include "keyboard.h"
#include "pic.h"
#include "port_io.h"

#define KEYBOARD_IRQ           1
#define KEYBOARD_DATA_PORT     0x60
#define KEYBOARD_STATUS_PORT   0x64

#define SCANCODE_BUFFER_SIZE   256
#define EVENT_BUFFER_SIZE      128

#define SCANCODE_BREAK         0x80
#define SCANCODE_EXTENDED      0xE0

static uint8_t scancode_buffer[SCANCODE_BUFFER_SIZE];
static uint32_t scancode_head;
static uint32_t scancode_tail;

static keyboard_event_t event_buffer[EVENT_BUFFER_SIZE];
static uint32_t event_head;
static uint32_t event_tail;

static int shift_pressed;
static int extended_scancode;
static int insert_mode;
static int insert_key_down;

static int keyboard_has_scancode(void);
static uint8_t keyboard_read_scancode(void);

static int scancode_buffer_push(uint8_t scancode)
{
    uint32_t next = (scancode_head + 1) % SCANCODE_BUFFER_SIZE;

    if (next == scancode_tail)
    {
        return 0;
    }

    scancode_buffer[scancode_head] = scancode;
    scancode_head = next;
    return 1;
}

static int event_buffer_push(keyboard_event_t event)
{
    uint32_t next = (event_head + 1) % EVENT_BUFFER_SIZE;

    if (next == event_tail)
    {
        return 0;
    }

    event_buffer[event_head] = event;
    event_head = next;
    return 1;
}

static void event_buffer_push_char(char c)
{
    keyboard_event_t event;

    event.type = KEYBOARD_EVENT_CHAR;
    event.character = c;
    event_buffer_push(event);
}

static void event_buffer_push_key(keyboard_event_type_t type)
{
    keyboard_event_t event;

    event.type = type;
    event.character = 0;
    event_buffer_push(event);
}

static char scancode_to_char(uint8_t scancode, int shift)
{
    static const char map[] =
    {
        0,   0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/'
    };

    static const char map_shift[] =
    {
        0,   0,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
        'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|',
        'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?'
    };

    if (scancode == 0x39)
    {
        return ' ';
    }

    if (scancode >= sizeof(map))
    {
        return 0;
    }

    return shift ? map_shift[scancode] : map[scancode];
}

static void handle_modifier(uint8_t scancode, int released)
{
    switch (scancode)
    {
        case 0x2A:
        case 0x36:
            shift_pressed = released ? 0 : 1;
            break;
        default:
            break;
    }
}

static void handle_extended_scancode(uint8_t scancode, int released)
{
    switch (scancode)
    {
        case 0x52:
            if (released)
            {
                insert_key_down = 0;
            }
            else if (!insert_key_down)
            {
                insert_key_down = 1;
                insert_mode = !insert_mode;
                event_buffer_push_key(KEYBOARD_EVENT_INSERT);
            }
            return;
        default:
            break;
    }

    if (released)
    {
        return;
    }

    switch (scancode)
    {
        case 0x48:
            event_buffer_push_key(KEYBOARD_EVENT_UP);
            break;
        case 0x50:
            event_buffer_push_key(KEYBOARD_EVENT_DOWN);
            break;
        case 0x4B:
            event_buffer_push_key(KEYBOARD_EVENT_LEFT);
            break;
        case 0x4D:
            event_buffer_push_key(KEYBOARD_EVENT_RIGHT);
            break;
        case 0x53:
            event_buffer_push_key(KEYBOARD_EVENT_DELETE);
            break;
        default:
            break;
    }
}

static void process_scancode(uint8_t scancode)
{
    if (scancode == SCANCODE_EXTENDED)
    {
        extended_scancode = 1;
        return;
    }

    int released = scancode & SCANCODE_BREAK;
    uint8_t code = scancode & 0x7F;

    if (extended_scancode)
    {
        extended_scancode = 0;
        handle_extended_scancode(code, released);
        return;
    }

    if (released)
    {
        handle_modifier(code, 1);
        return;
    }

    handle_modifier(code, 0);

    if (code == 0x2A || code == 0x36)
    {
        return;
    }

    event_buffer_push_char(scancode_to_char(code, shift_pressed));
}

void keyboard_initialize(void)
{
    scancode_head = 0;
    scancode_tail = 0;
    event_head = 0;
    event_tail = 0;
    shift_pressed = 0;
    extended_scancode = 0;
    insert_mode = 1;
    insert_key_down = 0;

    while (inb(KEYBOARD_STATUS_PORT) & 0x01)
    {
        inb(KEYBOARD_DATA_PORT);
    }

    pic_unmask_irq(KEYBOARD_IRQ);
}

void keyboard_handler(void)
{
    scancode_buffer_push(inb(KEYBOARD_DATA_PORT));
    pic_send_eoi(KEYBOARD_IRQ);
}

void keyboard_poll(void)
{
    while (keyboard_has_scancode())
    {
        process_scancode(keyboard_read_scancode());
    }
}

static int keyboard_has_scancode(void)
{
    return scancode_head != scancode_tail;
}

static uint8_t keyboard_read_scancode(void)
{
    uint8_t scancode;

    if (!keyboard_has_scancode())
    {
        return 0;
    }

    scancode = scancode_buffer[scancode_tail];
    scancode_tail = (scancode_tail + 1) % SCANCODE_BUFFER_SIZE;
    return scancode;
}

int keyboard_has_event(void)
{
    return event_head != event_tail;
}

keyboard_event_t keyboard_read_event(void)
{
    keyboard_event_t event;

    event.type = KEYBOARD_EVENT_CHAR;
    event.character = 0;

    if (!keyboard_has_event())
    {
        return event;
    }

    event = event_buffer[event_tail];
    event_tail = (event_tail + 1) % EVENT_BUFFER_SIZE;
    return event;
}

int keyboard_is_insert_mode(void)
{
    return insert_mode;
}
