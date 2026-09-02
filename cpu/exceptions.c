#include "exceptions.h"
#include "paging.h"
#include "process.h"
#include "terminal.h"

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

static void write_hex_field(const char *label, uint32_t value)
{
    char hex[11];

    terminal_write(label);
    uint_to_hex(value, hex);
    terminal_write(hex);
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

    terminal_write("CPU exception: ");
    terminal_write(name);
    terminal_write(" (");
    uint_to_hex(vector, hex);
    terminal_write(hex);
    terminal_write(")\n");

    if (process != NULL)
    {
        terminal_write("  Process: ");
        terminal_write(process->name);
        terminal_write(" pid=");
        uint_to_hex(process->pid, hex);
        terminal_write(hex);
        terminal_write("\n");
    }
    else
    {
        terminal_write("  Process: (none)\n");
    }

    write_hex_field("  EIP: ", frame->eip);
    terminal_write("\n");
    write_hex_field("  Error: ", frame->error_code);
    terminal_write("\n");
    terminal_write(user_fault ? "  Mode: user\n" : "  Mode: kernel\n");

    if (vector == 14)
    {
        __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
        write_hex_field("  CR2: ", fault_address);
        terminal_write("\n");
    }

    write_hex_field("  Fault CR3: ", fault_cr3);
    terminal_write("\n");
    write_hex_field("  Kernel CR3: ", kernel_cr3);
    terminal_write("\n");
    terminal_write(as_valid ? "  Address space: valid\n" : "  Address space: INVALID\n");

    if (user_fault)
    {
        terminal_write("  Terminating user task.\n");
        process_handle_exception(frame);
        exception_need_reschedule = 1;
        return;
    }

    terminal_write("  System halted.\n");
    __asm__ volatile ("cli");

    for (;;)
    {
        __asm__ volatile ("hlt");
    }
}

void exceptions_initialize(void)
{
}
