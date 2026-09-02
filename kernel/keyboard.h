#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

typedef enum
{
    KEYBOARD_EVENT_CHAR,
    KEYBOARD_EVENT_UP,
    KEYBOARD_EVENT_DOWN,
    KEYBOARD_EVENT_LEFT,
    KEYBOARD_EVENT_RIGHT,
    KEYBOARD_EVENT_INSERT,
    KEYBOARD_EVENT_DELETE
} keyboard_event_type_t;

typedef struct
{
    keyboard_event_type_t type;
    char character;
} keyboard_event_t;

void keyboard_initialize(void);
void keyboard_handler(void);
void keyboard_poll(void);

int keyboard_has_event(void);
keyboard_event_t keyboard_read_event(void);

int keyboard_is_insert_mode(void);

#endif
