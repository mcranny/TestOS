#include "arch/ioapic.h"
#include "arch/acpi.h"
#include "arch/apic.h"
#include "mm/paging.h"
#include "platform.h"

#define IOAPIC_REGSEL 0x00
#define IOAPIC_WINDOW 0x10

static volatile uint32_t *ioapic_mmio;
static uint32_t ioapic_gsi_base;
static int ioapic_ready;

static void ioapic_write(uint32_t reg, uint32_t value)
{
    ioapic_mmio[IOAPIC_REGSEL / 4] = reg;
    ioapic_mmio[IOAPIC_WINDOW / 4] = value;
}

static void ioapic_set_redir(uint32_t gsi, uint64_t entry)
{
    uint32_t index = 0x10 + (gsi - ioapic_gsi_base) * 2;
    ioapic_write(index, (uint32_t)entry);
    ioapic_write(index + 1, (uint32_t)(entry >> 32));
}

void ioapic_init(void)
{
    const struct acpi_madt_info *madt = acpi_madt();
    if (!madt || !madt->has_ioapic || madt->ioapic_address == 0) {
        ioapic_ready = 0;
        return;
    }
    ioapic_mmio = (volatile uint32_t *)map_mmio(madt->ioapic_address, 0x1000);
    if (!ioapic_mmio) {
        panic("ioapic: map failed");
    }
    ioapic_gsi_base = madt->ioapic_gsi_base;
    ioapic_ready = 1;
}

int ioapic_present(void)
{
    return ioapic_ready;
}

void ioapic_route_isa_irq(uint8_t isa_irq, uint8_t vector, uint32_t dest_lapic_id)
{
    const struct acpi_madt_info *madt = acpi_madt();
    uint32_t gsi;
    uint16_t flags;
    uint64_t entry;

    if (!ioapic_ready || !madt || isa_irq >= 16) {
        return;
    }

    gsi = madt->irq_gsi[isa_irq];
    flags = madt->irq_flags[isa_irq];

    /* Delivery: fixed, physical dest, masked=0 */
    entry = vector;
    if ((flags & 0x3) == 0x3) {
        entry |= (1ULL << 13); /* active low */
    }
    if ((flags & 0xc) == 0xc) {
        entry |= (1ULL << 15); /* level trigger */
    }
    entry |= ((uint64_t)dest_lapic_id << 56);
    ioapic_set_redir(gsi, entry);
}
