.section .rodata
.global _wget_blob_start
.global _wget_blob_end
_wget_blob_start:
.incbin "build/uefi-wget.elf"
_wget_blob_end:
