#include "input/input.h"

#define INPUT_QUEUE_SIZE 128

static input_event_t input_queue[INPUT_QUEUE_SIZE];
static uint32_t input_head;
static uint32_t input_tail;
static int shift_down; /* updated by HID keyboard modifiers */

void input_initialize(void)
{
    input_head = 0;
    input_tail = 0;
    shift_down = 0;
}

void input_push_event(const input_event_t *event)
{
    uint32_t next;

    if (event == NULL) {
        return;
    }

    next = (input_head + 1U) % INPUT_QUEUE_SIZE;
    if (next == input_tail) {
        return;
    }

    input_queue[input_head] = *event;
    input_head = next;
}

int input_pop_event(input_event_t *event)
{
    if (event == NULL || input_head == input_tail) {
        return 0;
    }

    *event = input_queue[input_tail];
    input_tail = (input_tail + 1U) % INPUT_QUEUE_SIZE;
    return 1;
}

char input_keycode_to_ascii(input_keycode_t key, int shift)
{
    static const char lower[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static const char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ)!@#$%^&*(";
    uint32_t index;

    if (key >= INPUT_KEY_A && key <= INPUT_KEY_Z) {
        index = (uint32_t)(key - INPUT_KEY_A);
        return shift ? upper[index] : lower[index];
    }
    if (key >= INPUT_KEY_0 && key <= INPUT_KEY_9) {
        index = (uint32_t)(key - INPUT_KEY_0);
        return shift ? upper[26U + index] : lower[26U + index];
    }
    switch (key) {
        case INPUT_KEY_SPACE: return ' ';
        case INPUT_KEY_TAB: return '\t';
        case INPUT_KEY_ENTER: return '\n';
        case INPUT_KEY_BACKSPACE: return '\b';
        default: return 0;
    }
}

int input_has_char(void)
{
    uint32_t index = input_tail;

    while (index != input_head) {
        input_event_t *event = &input_queue[index];
        if (event->type == INPUT_KEY_DOWN &&
            input_keycode_to_ascii(event->key, shift_down) != 0) {
            return 1;
        }
        if (event->type == INPUT_KEY_DOWN &&
            (event->key == INPUT_KEY_LEFT || event->key == INPUT_KEY_RIGHT ||
             event->key == INPUT_KEY_ARROW_UP || event->key == INPUT_KEY_ARROW_DOWN)) {
            return 1;
        }
        index = (index + 1U) % INPUT_QUEUE_SIZE;
    }
    return 0;
}

void input_set_shift(int down)
{
    shift_down = down ? 1 : 0;
}

char input_getchar(void)
{
    input_event_t event;

    while (input_pop_event(&event)) {
        if (event.type == INPUT_KEY_DOWN) {
            if (event.key == INPUT_KEY_LEFT) {
                return 0x01;
            }
            if (event.key == INPUT_KEY_RIGHT) {
                return 0x02;
            }
            if (event.key == INPUT_KEY_ARROW_UP) {
                return 0x03;
            }
            if (event.key == INPUT_KEY_ARROW_DOWN) {
                return 0x04;
            }
            return input_keycode_to_ascii(event.key, shift_down);
        }
    }
    return 0;
}
