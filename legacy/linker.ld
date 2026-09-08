ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* Multiboot v1 scanners inspect only the first 8192 bytes of the file. */
    .multiboot ALIGN(4) :
    {
        KEEP(*(.multiboot))
    }

    .text :
    {
        *(.text)
    }

    .rodata :
    {
        *(.rodata)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
    }

    . = ALIGN(4096);

    _kernel_end = .;
}
