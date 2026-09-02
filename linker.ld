ENTRY(_start)

SECTIONS
{
    . = 1M;

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
