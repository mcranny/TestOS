#ifndef TESTOS_UEFI_INPUT_INPUT_H
#define TESTOS_UEFI_INPUT_INPUT_H

#include "types.h"

typedef enum input_event_type
{
    INPUT_KEY_DOWN,
    INPUT_KEY_UP,
    INPUT_MOUSE_MOVE,
    INPUT_MOUSE_BUTTON
} input_event_type_t;

typedef enum input_keycode
{
    INPUT_KEY_NONE = 0,
    INPUT_KEY_ESC,
    INPUT_KEY_BACKSPACE,
    INPUT_KEY_TAB,
    INPUT_KEY_ENTER,
    INPUT_KEY_SPACE,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_ARROW_UP,
    INPUT_KEY_ARROW_DOWN,
    INPUT_KEY_A,
    INPUT_KEY_B,
    INPUT_KEY_C,
    INPUT_KEY_D,
    INPUT_KEY_E,
    INPUT_KEY_F,
    INPUT_KEY_G,
    INPUT_KEY_H,
    INPUT_KEY_I,
    INPUT_KEY_J,
    INPUT_KEY_K,
    INPUT_KEY_L,
    INPUT_KEY_M,
    INPUT_KEY_N,
    INPUT_KEY_O,
    INPUT_KEY_P,
    INPUT_KEY_Q,
    INPUT_KEY_R,
    INPUT_KEY_S,
    INPUT_KEY_T,
    INPUT_KEY_U,
    INPUT_KEY_V,
    INPUT_KEY_W,
    INPUT_KEY_X,
    INPUT_KEY_Y,
    INPUT_KEY_Z,
    INPUT_KEY_0,
    INPUT_KEY_1,
    INPUT_KEY_2,
    INPUT_KEY_3,
    INPUT_KEY_4,
    INPUT_KEY_5,
    INPUT_KEY_6,
    INPUT_KEY_7,
    INPUT_KEY_8,
    INPUT_KEY_9
} input_keycode_t;

typedef struct input_event
{
    input_event_type_t type;
    input_keycode_t key;
    int32_t mouse_x;
    int32_t mouse_y;
    uint8_t mouse_button;
    int mouse_pressed;
} input_event_t;

void input_initialize(void);
void input_set_shift(int down);
void input_push_event(const input_event_t *event);
int input_pop_event(input_event_t *event);
char input_keycode_to_ascii(input_keycode_t key, int shift);
int input_has_char(void);
char input_getchar(void);

#endif
