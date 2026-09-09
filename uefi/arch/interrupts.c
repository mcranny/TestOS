#include "arch/interrupts.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "drivers/pic.h"
#include "task/process.h"
#include "platform.h"

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);
extern void isr32(void); extern void isr33(void); extern void isr34(void); extern void isr35(void);
extern void isr36(void); extern void isr37(void); extern void isr38(void); extern void isr39(void);
extern void isr40(void); extern void isr41(void); extern void isr42(void); extern void isr43(void);
extern void isr44(void); extern void isr45(void); extern void isr46(void); extern void isr47(void);

static void (*irq_handlers[16])(void *);

static const char *exception_name(uint64_t vector)
{
    static const char *names[] = {
        "#DE", "#DB", "NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
        "#DF", "CSO", "#TS", "#NP", "#SS", "#GP", "#PF", "RSV",
        "#MF", "#AC", "#MC", "#XM", "#VE", "#CP"
    };
    if (vector < sizeof(names) / sizeof(names[0])) {
        return names[vector];
    }
    return "EXC";
}

void isr_dispatch(struct interrupt_frame *frame)
{
    if (frame->vector < 32) {
        log_error(exception_name(frame->vector));
        log_hex64("RIP=", frame->rip);
        log_hex64("ERR=", frame->error_code);
        if (frame->vector == 14) {
            log_hex64("CR2=", read_cr2());
        }
        /* Ring-3 faults kill the process; kernel faults still panic. */
        if ((frame->cs & 3) == 3) {
            process_t *p = process_get_current();
            if (p && !p->protected) {
                log_warn("terminating user process after exception");
                log_hex64("PID=", p->pid);
                /* Never returns — same path as SYS_EXIT / process_terminate. */
                process_exit(139);
            }
        }
        panic("CPU exception");
    }

    if (frame->vector >= 32 && frame->vector <= 47) {
        uint8_t irq = (uint8_t)(frame->vector - 32);
        if (irq_handlers[irq]) {
            irq_handlers[irq](frame);
        }
        pic_eoi(irq);
        if (irq == 0) {
            scheduler_on_irq_exit();
        }
        return;
    }

    panic("unexpected interrupt vector");
}

void irq_register(uint8_t irq, void (*handler)(void *))
{
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void interrupts_init(void)
{
    void (*stubs[])(void) = {
        isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
        isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
        isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
        isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
        isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
        isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
    };
    uint8_t i;

    for (i = 0; i < 16; i++) {
        irq_handlers[i] = 0;
    }

    for (i = 0; i < 48; i++) {
        uint8_t ist = (i == 8) ? 1 : 0; /* double fault uses IST1 */
        idt_set_gate(i, (void *)stubs[i], ist, 0x8E);
    }
}
