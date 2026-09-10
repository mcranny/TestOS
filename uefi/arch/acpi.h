#ifndef TESTOS_UEFI_ARCH_ACPI_H
#define TESTOS_UEFI_ARCH_ACPI_H

#include "types.h"

struct acpi_madt_info {
    uint64_t lapic_address;
    uint64_t ioapic_address;
    uint32_t ioapic_id;
    uint32_t ioapic_gsi_base;
    int has_ioapic;
    /* ISA IRQ → GSI overrides (identity by default). */
    uint32_t irq_gsi[16];
    uint16_t irq_flags[16]; /* ACPI MADT polarity/trigger bits */
};

int acpi_init(void *rsdp_addr);
const struct acpi_madt_info *acpi_madt(void);

#endif
