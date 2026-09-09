#include "drivers/pit.h"
#include "arch/io.h"
#include "task/process.h"

static volatile uint64_t ticks;
static uint64_t hz = 100;

void pit_init(uint32_t frequency)
{
    uint32_t divisor;
    if (frequency == 0) {
        frequency = 100;
    }
    hz = frequency;
    divisor = 1193182U / frequency;
    if (divisor == 0) {
        divisor = 1;
    }
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xff));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xff));
    ticks = 0;
}

void timer_irq_handler(void *frame)
{
    (void)frame;
    ticks++;
    /* Net stack is not IRQ-safe; tcp/http/arp run from ethernet_poll. */
    scheduler_tick();
}

uint64_t timer_ticks(void)
{
    return ticks;
}

uint64_t timer_hz(void)
{
    return hz;
}
