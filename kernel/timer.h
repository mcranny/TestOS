#ifndef TIMER_H
#define TIMER_H

#include "types.h"

#define TIMER_FREQUENCY 100

void timer_initialize(void);
void timer_handler(void);

uint32_t timer_get_ticks(void);
uint32_t timer_get_uptime_seconds(void);

#endif
