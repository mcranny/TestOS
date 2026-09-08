ENTRY(_start)

SECTIONS {
    . = 0xffffffff80000000;
    __kernel_start = .;

    .limine_requests_start : {
        KEEP(*(.limine_requests_start))
    }
    .limine_requests : {
        KEEP(*(.limine_requests))
    }
    .limine_requests_end : {
        KEEP(*(.limine_requests_end))
    }

    /* Limine forbids PHDRs with different permissions sharing a page. */
    . = ALIGN(4096);
    .text : { *(.text .text.*) }

    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) }

    . = ALIGN(4096);
    .data : { *(.data .data.*) }

    . = ALIGN(4096);
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(4096);
    __kernel_end = .;
}
