#include "exceptions.h"
#include "paging.h"
#include "process.h"
#include "terminal.h"
#include "log.h"
#include "serial.h"

volatile int exception_need_reschedule = 0;

static const char *exception_names[] =
{
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

static void uint_to_hex(uint32_t value, char *buffer)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    int index;

    buffer[0] = '0';
    buffer[1] = 'x';

    for (index = 0; index < 8; index++)
    {
        buffer[2 + index] =
            hex_digits[(value >> ((7 - index) * 4)) & 0x0FU];
    }

    buffer[10] = '\0';
}

static void emit(const char *text)
{
    terminal_write(text);
    serial_write(text);
}

static void write_hex_field(const char *label, uint32_t value)
{
    char hex[11];

    emit(label);
    uint_to_hex(value, hex);
    emit(hex);
}

void exception_handler(exception_frame_t *frame)
{
    char hex[11];
    const char *name = "Unknown Exception";
    uint32_t vector = frame->interrupt_number;
    uint32_t fault_cr3 = 0;
    uint32_t kernel_cr3 = 0;
    uint32_t fault_address = 0;
    process_t *process;
    int user_fault;
    int as_valid = 0;
    address_space_t *kernel_as;

    __asm__ volatile ("mov %%cr3, %0" : "=r"(fault_cr3));

    /* Switch to kernel CR3 before touching any process_t fields. */
    kernel_as = paging_get_kernel_address_space();
    address_space_switch(kernel_as);

    if (address_space_is_valid(kernel_as))
    {
        kernel_cr3 = kernel_as->page_directory_phys;
    }

    process = process_get_current();

    if (process != NULL)
    {
        as_valid = address_space_is_valid(process->address_space);
    }

    user_fault = (frame->cs & 3) == 3;

    if (vector < 32)
    {
        name = exception_names[vector];
    }

    klog(KLOG_ERROR, "CPU", name);
    emit(" (");
    uint_to_hex(vector, hex);
    emit(hex);
    emit(")\n");

    if (process != NULL)
    {
        emit("  Process: ");
        emit(process->name);
        emit(" pid=");
        uint_to_hex(process->pid, hex);
        emit(hex);
        emit("\n");
    }
    else
    {
        emit("  Process: (none)\n");
    }

    write_hex_field("  EIP: ", frame->eip);
    emit("\n");
    write_hex_field("  Error: ", frame->error_code);
    emit("\n");
    emit(user_fault ? "  Mode: user\n" : "  Mode: kernel\n");

    if (vector == 14)
    {
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        write_hex_field("  CR2: ", fault_address);
        emit("\n");
    }

    write_hex_field("  Fault CR3: ", fault_cr3);
    emit("\n");
    write_hex_field("  Kernel CR3: ", kernel_cr3);
    emit("\n");
    emit(as_valid ? "  Address space: valid\n" : "  Address space: INVALID\n");

    if (user_fault)
    {
        klog(KLOG_WARN, "PROC", "Terminating user task after exception");
        process_handle_exception(frame);
        exception_need_reschedule = 1;
        return;
    }

    panic("CPU", name);
}

void exceptions_initialize(void)
{
}
