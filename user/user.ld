ENTRY(_start)

SECTIONS
{
    . = 0x00200000;

    .text :
    {
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss :
    {
        *(.bss*)
    }

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
