#include "mouse.h"
#include "log.h"
#include "pic.h"
#include "port_io.h"

#define MOUSE_IRQ            12
#define MOUSE_CASCADE_IRQ    2

#define PS2_DATA_PORT        0x60
#define PS2_STATUS_PORT      0x64
#define PS2_COMMAND_PORT     0x64

#define PS2_STATUS_OUTPUT    0x01
#define PS2_STATUS_INPUT     0x02

#define PS2_CMD_READ_CONFIG  0x20
#define PS2_CMD_WRITE_CONFIG 0x60
#define PS2_CMD_ENABLE_AUX   0xA8
#define PS2_CMD_WRITE_AUX    0xD4

#define MOUSE_CMD_ENABLE     0xF4

#define MOUSE_CONFIG_IRQ12   0x02
#define MOUSE_CONFIG_AUX_CLOCK_DISABLE 0x20

static mouse_state_t mouse_state;
static uint8_t packet_bytes[3];
static uint8_t packet_index;
static int mouse_ready;

static void ps2_wait_input_clear(void)
{
    uint32_t timeout = 100000U;

    while (timeout-- > 0)
    {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT) == 0)
        {
            return;
        }
    }
}

static void ps2_wait_output_full(void)
{
    uint32_t timeout = 100000U;

    while (timeout-- > 0)
    {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) != 0)
        {
            return;
        }
    }
}

static void ps2_write_command(uint8_t command)
{
    ps2_wait_input_clear();
    outb(PS2_COMMAND_PORT, command);
}

static void ps2_write_data(uint8_t value)
{
    ps2_wait_input_clear();
    outb(PS2_DATA_PORT, value);
}

static uint8_t ps2_read_data(void)
{
    ps2_wait_output_full();
    return inb(PS2_DATA_PORT);
}

static void mouse_write(uint8_t value)
{
    ps2_write_command(PS2_CMD_WRITE_AUX);
    ps2_write_data(value);
}

static void mouse_log_state_debug(void)
{
    char line[64];
    uint32_t i = 0;
    int x = mouse_state.x;
    int y = mouse_state.y;
    int negative;
    uint32_t abs_value;
    char number[12];
    int len;

    line[i++] = 'x';
    line[i++] = '=';

    negative = x < 0;
    abs_value = negative ? (uint32_t)(-x) : (uint32_t)x;
    if (negative)
    {
        line[i++] = '-';
    }

    if (abs_value == 0)
    {
        line[i++] = '0';
    }
    else
    {
        len = 0;
        while (abs_value > 0)
        {
            number[len++] = (char)('0' + (abs_value % 10U));
            abs_value /= 10U;
        }
        while (len > 0)
        {
            line[i++] = number[--len];
        }
    }

    line[i++] = ' ';
    line[i++] = 'y';
    line[i++] = '=';

    negative = y < 0;
    abs_value = negative ? (uint32_t)(-y) : (uint32_t)y;
    if (negative)
    {
        line[i++] = '-';
    }

    if (abs_value == 0)
    {
        line[i++] = '0';
    }
    else
    {
        len = 0;
        while (abs_value > 0)
        {
            number[len++] = (char)('0' + (abs_value % 10U));
            abs_value /= 10U;
        }
        while (len > 0)
        {
            line[i++] = number[--len];
        }
    }

    line[i++] = ' ';
    line[i++] = 'l';
    line[i++] = 'e';
    line[i++] = 'f';
    line[i++] = 't';
    line[i++] = '=';
    line[i++] = (char)('0' + (mouse_state.left != 0));
    line[i++] = ' ';
    line[i++] = 'r';
    line[i++] = 'i';
    line[i++] = 'g';
    line[i++] = 'h';
    line[i++] = 't';
    line[i++] = '=';
    line[i++] = (char)('0' + (mouse_state.right != 0));
    line[i++] = ' ';
    line[i++] = 'm';
    line[i++] = 'i';
    line[i++] = 'd';
    line[i++] = '=';
    line[i++] = (char)('0' + (mouse_state.middle != 0));
    line[i] = '\0';

    klog(KLOG_DEBUG, "MOUSE", line);
}

static void mouse_process_byte(uint8_t value)
{
    mouse_event_t event;
    int8_t dx;
    int8_t dy;

    if (packet_index == 0)
    {
        /* Bit 3 must be set in the first byte of a valid PS/2 packet. */
        if ((value & 0x08) == 0)
        {
            return;
        }
    }

    packet_bytes[packet_index++] = value;
    if (packet_index < 3)
    {
        return;
    }

    packet_index = 0;

    dx = (int8_t)packet_bytes[1];
    dy = (int8_t)packet_bytes[2];

    /* Discard overflow packets. */
    if ((packet_bytes[0] & 0xC0) != 0)
    {
        return;
    }

    event.dx = dx;
    event.dy = -dy; /* PS/2 Y is opposite screen coordinates. */
    event.left = (packet_bytes[0] & 0x01) != 0;
    event.right = (packet_bytes[0] & 0x02) != 0;
    event.middle = (packet_bytes[0] & 0x04) != 0;

    mouse_state.x += event.dx;
    mouse_state.y += event.dy;
    mouse_state.left = event.left;
    mouse_state.right = event.right;
    mouse_state.middle = event.middle;

    mouse_log_state_debug();
}

void mouse_initialize(void)
{
    uint8_t config;
    uint8_t response;

    mouse_state.x = 0;
    mouse_state.y = 0;
    mouse_state.left = 0;
    mouse_state.right = 0;
    mouse_state.middle = 0;
    packet_index = 0;
    mouse_ready = 0;

    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT)
    {
        inb(PS2_DATA_PORT);
    }

    ps2_write_command(PS2_CMD_ENABLE_AUX);

    ps2_write_command(PS2_CMD_READ_CONFIG);
    config = ps2_read_data();
    config |= MOUSE_CONFIG_IRQ12;
    config &= (uint8_t)~MOUSE_CONFIG_AUX_CLOCK_DISABLE;
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    mouse_write(MOUSE_CMD_ENABLE);
    response = ps2_read_data();
    if (response != 0xFA)
    {
        klog(KLOG_WARN, "MOUSE", "Mouse enable not acknowledged");
    }

    pic_unmask_irq(MOUSE_CASCADE_IRQ);
    pic_unmask_irq(MOUSE_IRQ);

    mouse_ready = 1;
    klog(KLOG_INFO, "MOUSE", "PS/2 mouse initialized");
}

void mouse_handler(void)
{
    uint8_t value;

    value = inb(PS2_DATA_PORT);
    if (mouse_ready)
    {
        mouse_process_byte(value);
    }

    pic_send_eoi(MOUSE_IRQ);
}

void mouse_get_state(mouse_state_t *out)
{
    if (out == NULL)
    {
        return;
    }

    *out = mouse_state;
}
