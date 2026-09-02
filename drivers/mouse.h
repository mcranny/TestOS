#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"

typedef struct
{
    int dx;
    int dy;
    int left;
    int right;
    int middle;
} mouse_event_t;

typedef struct
{
    int x;
    int y;
    int left;
    int right;
    int middle;
} mouse_state_t;

void mouse_initialize(void);
void mouse_handler(void);
void mouse_get_state(mouse_state_t *out);

#endif
