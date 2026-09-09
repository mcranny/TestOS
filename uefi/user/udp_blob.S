.section .rodata
.global _udp_blob_start
.global _udp_blob_end
_udp_blob_start:
.incbin "build/uefi-udp.elf"
_udp_blob_end:
