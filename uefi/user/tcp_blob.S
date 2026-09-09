.section .rodata
.global _tcp_blob_start
.global _tcp_blob_end
_tcp_blob_start:
.incbin "build/uefi-tcp.elf"
_tcp_blob_end:
