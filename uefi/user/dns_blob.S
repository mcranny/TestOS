.section .rodata
.global _dns_blob_start
.global _dns_blob_end
_dns_blob_start:
.incbin "build/uefi-dns.elf"
_dns_blob_end:
