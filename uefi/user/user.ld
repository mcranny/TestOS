ENTRY(_start)
SECTIONS
{
    . = 0x400000;
    .text : {
        *(.text .text.*)
    }
    . = ALIGN(4096);
    .rodata : {
        *(.rodata .rodata.*)
    }
    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    }
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
}
