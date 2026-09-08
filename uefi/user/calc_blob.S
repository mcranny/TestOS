.section .rodata
.global _calc_blob_start
.global _calc_blob_end
.align 8
_calc_blob_start:
    .incbin "build/uefi-calc.elf"
_calc_blob_end:
