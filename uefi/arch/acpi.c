#include "arch/acpi.h"
#include "mm/pmm.h"
#include "platform.h"
#include "lib/string.h"

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

static struct acpi_madt_info madt_info;
static int madt_ready;

static int checksum_ok(const void *table, uint32_t length)
{
    const uint8_t *p = table;
    uint8_t sum = 0;
    uint32_t i;
    for (i = 0; i < length; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum == 0;
}

static void *map_table(uint64_t phys)
{
    return phys_to_virt(phys);
}

static void parse_madt(struct acpi_madt *madt)
{
    uint8_t *end;
    uint8_t *ptr;
    uint32_t i;

    memset(&madt_info, 0, sizeof(madt_info));
    for (i = 0; i < 16; i++) {
        madt_info.irq_gsi[i] = i;
        madt_info.irq_flags[i] = 0;
    }

    madt_info.lapic_address = madt->lapic_address;
    end = (uint8_t *)madt + madt->header.length;
    ptr = (uint8_t *)(madt + 1);

    while (ptr + 2 <= end) {
        struct acpi_madt_entry *e = (struct acpi_madt_entry *)ptr;
        if (e->length < 2 || ptr + e->length > end) {
            break;
        }
        if (e->type == 1 && e->length >= 12) {
            /* IO APIC */
            madt_info.ioapic_id = ptr[2];
            madt_info.ioapic_address = *(uint32_t *)(ptr + 4);
            madt_info.ioapic_gsi_base = *(uint32_t *)(ptr + 8);
            madt_info.has_ioapic = 1;
        } else if (e->type == 2 && e->length >= 10) {
            /* Interrupt source override */
            uint8_t irq = ptr[3];
            uint32_t gsi = *(uint32_t *)(ptr + 4);
            uint16_t flags = *(uint16_t *)(ptr + 8);
            if (irq < 16) {
                madt_info.irq_gsi[irq] = gsi;
                madt_info.irq_flags[irq] = flags;
            }
        } else if (e->type == 5 && e->length >= 12) {
            madt_info.lapic_address = *(uint64_t *)(ptr + 4);
        }
        ptr += e->length;
    }
    madt_ready = 1;
}

static int find_madt_in_xsdt(struct acpi_sdt_header *xsdt)
{
    uint32_t entries;
    uint32_t i;
    uint64_t *ptrs;

    if (xsdt->length < sizeof(*xsdt)) {
        return -1;
    }
    entries = (xsdt->length - (uint32_t)sizeof(*xsdt)) / 8U;
    ptrs = (uint64_t *)(xsdt + 1);
    for (i = 0; i < entries; i++) {
        struct acpi_sdt_header *hdr = map_table(ptrs[i]);
        if (!hdr || hdr->length < sizeof(*hdr)) {
            continue;
        }
        if (hdr->signature[0] == 'A' && hdr->signature[1] == 'P' &&
            hdr->signature[2] == 'I' && hdr->signature[3] == 'C' &&
            checksum_ok(hdr, hdr->length)) {
            parse_madt((struct acpi_madt *)hdr);
            return 0;
        }
    }
    return -1;
}

static int find_madt_in_rsdt(struct acpi_sdt_header *rsdt)
{
    uint32_t entries;
    uint32_t i;
    uint32_t *ptrs;

    if (rsdt->length < sizeof(*rsdt)) {
        return -1;
    }
    entries = (rsdt->length - (uint32_t)sizeof(*rsdt)) / 4U;
    ptrs = (uint32_t *)(rsdt + 1);
    for (i = 0; i < entries; i++) {
        struct acpi_sdt_header *hdr = map_table(ptrs[i]);
        if (!hdr || hdr->length < sizeof(*hdr)) {
            continue;
        }
        if (hdr->signature[0] == 'A' && hdr->signature[1] == 'P' &&
            hdr->signature[2] == 'I' && hdr->signature[3] == 'C' &&
            checksum_ok(hdr, hdr->length)) {
            parse_madt((struct acpi_madt *)hdr);
            return 0;
        }
    }
    return -1;
}

int acpi_init(void *rsdp_addr)
{
    struct acpi_rsdp *rsdp;
    if (!rsdp_addr) {
        return -1;
    }
    rsdp = rsdp_addr;
    /* Limine may hand a physical address; try HHDM if signature looks wrong. */
    if (rsdp->signature[0] != 'R') {
        rsdp = phys_to_virt((uint64_t)(uintptr_t)rsdp_addr);
    }
    if (!rsdp || rsdp->signature[0] != 'R' || rsdp->signature[1] != 'S' ||
        rsdp->signature[2] != 'D' || rsdp->signature[3] != ' ' ||
        rsdp->signature[4] != 'P' || rsdp->signature[5] != 'T' ||
        rsdp->signature[6] != 'R' || rsdp->signature[7] != ' ') {
        return -1;
    }

    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        struct acpi_sdt_header *xsdt = map_table(rsdp->xsdt_address);
        if (xsdt && checksum_ok(xsdt, xsdt->length) &&
            xsdt->signature[0] == 'X' && find_madt_in_xsdt(xsdt) == 0) {
            return 0;
        }
    }
    if (rsdp->rsdt_address != 0) {
        struct acpi_sdt_header *rsdt = map_table(rsdp->rsdt_address);
        if (rsdt && checksum_ok(rsdt, rsdt->length) &&
            rsdt->signature[0] == 'R' && find_madt_in_rsdt(rsdt) == 0) {
            return 0;
        }
    }
    return -1;
}

const struct acpi_madt_info *acpi_madt(void)
{
    return madt_ready ? &madt_info : NULL;
}
