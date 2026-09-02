#include "timer.h"
#include "process.h"
#include "pic.h"
#include "port_io.h"

#define PIT_CHANNEL0_DATA 0x40
#define PIT_COMMAND       0x43
#define PIT_DIVISOR       (1193180 / TIMER_FREQUENCY)

#define TIMER_IRQ 0

static volatile uint32_t timer_ticks;

void timer_initialize(void)
{
    timer_ticks = 0;

    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, PIT_DIVISOR & 0xFF);
    outb(PIT_CHANNEL0_DATA, (PIT_DIVISOR >> 8) & 0xFF);

    pic_unmask_irq(TIMER_IRQ);
}

void timer_handler(void)
{
    timer_ticks++;
    pic_send_eoi(TIMER_IRQ);
    scheduler_wake_sleepers();
    scheduler_tick();
}

uint32_t timer_get_ticks(void)
{
    return timer_ticks;
}

uint32_t timer_get_uptime_seconds(void)
{
    return timer_ticks / TIMER_FREQUENCY;
}
