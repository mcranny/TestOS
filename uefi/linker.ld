ENTRY(_start)

SECTIONS {
    . = 0xffffffff80000000;

    .limine_requests_start : {
        KEEP(*(.limine_requests_start))
    }
    .limine_requests : {
        KEEP(*(.limine_requests))
    }
    .limine_requests_end : {
        KEEP(*(.limine_requests_end))
    }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(COMMON) *(.bss .bss.*) }
}
