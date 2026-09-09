#include "mm/paging.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "arch/io.h"
#include "drivers/console.h"
#include "lib/string.h"
#include "platform.h"

#define PAGE_SIZE 4096ULL
#define PTE_ADDR_MASK 0x000ffffffffff000ULL

static uint64_t *pml4;
static uint64_t kernel_pml4_phys;
static address_space_t kernel_as;

static void memset64(void *dst, uint8_t value, uint64_t size)
{
    uint8_t *p = dst;
    uint64_t i;
    for (i = 0; i < size; i++) {
        p[i] = value;
    }
}

static uint64_t *table_ptr(uint64_t entry)
{
    return (uint64_t *)phys_to_virt(entry & PTE_ADDR_MASK);
}

static uint64_t *get_or_alloc_table_flags(uint64_t *parent, uint64_t index, uint64_t flags)
{
    uint64_t entry = parent[index];
    uint64_t phys;
    uint64_t *table;
    if (entry & PAGE_PRESENT) {
        if (entry & (1ULL << 7)) {
            panic("paging: large page blocks table walk");
        }
        /* Ensure USER bit propagates on existing intermediate entries when needed. */
        if ((flags & PAGE_USER) && !(entry & PAGE_USER)) {
            parent[index] = entry | PAGE_USER;
        }
        return table_ptr(entry);
    }
    phys = pmm_alloc_frame();
    if (phys == 0) {
        panic("paging: out of frames for page table");
    }
    table = (uint64_t *)phys_to_virt(phys);
    memset64(table, 0, PAGE_SIZE);
    parent[index] = phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    return table;
}

static uint64_t *get_or_alloc_table(uint64_t *parent, uint64_t index)
{
    return get_or_alloc_table_flags(parent, index, 0);
}

static int map_page_in(uint64_t *root, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t pml4e = (virt >> 39) & 0x1ff;
    uint64_t pdpte = (virt >> 30) & 0x1ff;
    uint64_t pde = (virt >> 21) & 0x1ff;
    uint64_t pte = (virt >> 12) & 0x1ff;
    uint64_t mid = (flags & PAGE_USER) ? PAGE_USER : 0;

    if (!root) return -1;
    if ((virt % PAGE_SIZE) != 0 || (phys % PAGE_SIZE) != 0) return -1;

    pdpt = get_or_alloc_table_flags(root, pml4e, mid);
    pd = get_or_alloc_table_flags(pdpt, pdpte, mid);
    pt = get_or_alloc_table_flags(pd, pde, mid);
    pt[pte] = (phys & PTE_ADDR_MASK) | (flags & ~PTE_ADDR_MASK) | PAGE_PRESENT;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

static void clone_pml4_from_limine(void)
{
    uint64_t old_cr3 = read_cr3() & PTE_ADDR_MASK;
    uint64_t *old_pml4 = (uint64_t *)phys_to_virt(old_cr3);
    uint64_t new_phys = pmm_alloc_frame();
    uint64_t i;

    if (new_phys == 0) {
        panic("paging: cannot allocate PML4");
    }
    pml4 = (uint64_t *)phys_to_virt(new_phys);
    for (i = 0; i < 512; i++) {
        pml4[i] = old_pml4[i];
    }
    kernel_pml4_phys = new_phys;
    kernel_as.pml4_phys = new_phys;
    write_cr3(new_phys);
}

int map_page(uint64_t virt, uint64_t phys, uint64_t flags)
{
    return map_page_in(pml4, virt, phys, flags);
}

int unmap_page(uint64_t virt)
{
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t pml4e = (virt >> 39) & 0x1ff;
    uint64_t pdpte = (virt >> 30) & 0x1ff;
    uint64_t pde = (virt >> 21) & 0x1ff;
    uint64_t pte = (virt >> 12) & 0x1ff;

    if (!pml4 || !(pml4[pml4e] & PAGE_PRESENT)) return -1;
    pdpt = table_ptr(pml4[pml4e]);
    if (!(pdpt[pdpte] & PAGE_PRESENT) || (pdpt[pdpte] & (1ULL << 7))) return -1;
    pd = table_ptr(pdpt[pdpte]);
    if (!(pd[pde] & PAGE_PRESENT) || (pd[pde] & (1ULL << 7))) return -1;
    pt = table_ptr(pd[pde]);
    pt[pte] = 0;
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

#define MMIO_WINDOW_BASE 0xffffd00000000000ULL
static uint64_t mmio_next_virt = MMIO_WINDOW_BASE;

void *map_mmio(uint64_t phys, uint64_t size)
{
    uint64_t offset;
    uint64_t map_start;
    uint64_t map_end;
    uint64_t page;
    uint64_t virt_base;
    uint64_t mapped = 0;

    if (size == 0 || !pml4) {
        return NULL;
    }

    offset = phys & (PAGE_SIZE - 1ULL);
    map_start = phys & ~(PAGE_SIZE - 1ULL);
    map_end = phys + size;
    if (map_end < phys) {
        return NULL;
    }
    map_end = (map_end + PAGE_SIZE - 1ULL) & ~(PAGE_SIZE - 1ULL);

    virt_base = mmio_next_virt;
    for (page = map_start; page < map_end; page += PAGE_SIZE) {
        if (map_page(virt_base + mapped, page,
                     PAGE_PRESENT | PAGE_WRITABLE | PAGE_PCD | PAGE_NX) != 0) {
            while (mapped > 0) {
                mapped -= PAGE_SIZE;
                unmap_page(virt_base + mapped);
            }
            return NULL;
        }
        mapped += PAGE_SIZE;
    }

    mmio_next_virt = virt_base + mapped;
    return (void *)(uintptr_t)(virt_base + offset);
}

void paging_probe(void)
{
    uint64_t phys = pmm_alloc_frame();
    volatile uint64_t *via_hhdm;
    volatile uint64_t *via_map;
    const uint64_t probe_virt = 0xffffff0000000000ULL;

    if (phys == 0) {
        panic("paging probe: alloc failed");
    }

    via_hhdm = (volatile uint64_t *)phys_to_virt(phys);
    via_hhdm[0] = 0x546573744f530064ULL;
    if (via_hhdm[0] != 0x546573744f530064ULL) {
        panic("paging probe: HHDM write/read failed");
    }

    if (map_page(probe_virt, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        panic("paging probe: map_page failed");
    }
    via_map = (volatile uint64_t *)(uintptr_t)probe_virt;
    if (via_map[0] != 0x546573744f530064ULL) {
        panic("paging probe: mapped read failed");
    }
    via_map[0] = 0;
    unmap_page(probe_virt);
    pmm_free_frame(phys);
    console_puts("paging probe OK\n");
}

address_space_t *address_space_kernel(void)
{
    return &kernel_as;
}

address_space_t *address_space_create(void)
{
    address_space_t *as;
    uint64_t phys;
    uint64_t *new_pml4;
    uint64_t *kern_pml4;
    uint64_t i;

    as = (address_space_t *)kmalloc(sizeof(address_space_t));
    if (!as) return NULL;
    phys = pmm_alloc_frame();
    if (!phys) {
        kfree(as);
        return NULL;
    }
    new_pml4 = (uint64_t *)phys_to_virt(phys);
    memset64(new_pml4, 0, PAGE_SIZE);
    kern_pml4 = (uint64_t *)phys_to_virt(kernel_pml4_phys);
    /* Share kernel half (entries 256..511). */
    for (i = 256; i < 512; i++) {
        new_pml4[i] = kern_pml4[i];
    }
    as->pml4_phys = phys;
    return as;
}

void address_space_destroy(address_space_t *as)
{
    uint64_t *root;
    uint64_t i, j, k, l;

    if (!as || as == &kernel_as) return;
    root = (uint64_t *)phys_to_virt(as->pml4_phys);
    /* Free user-half page tables only (0..255). */
    for (i = 0; i < 256; i++) {
        if (!(root[i] & PAGE_PRESENT) || (root[i] & (1ULL << 7))) continue;
        {
            uint64_t *pdpt = table_ptr(root[i]);
            for (j = 0; j < 512; j++) {
                if (!(pdpt[j] & PAGE_PRESENT) || (pdpt[j] & (1ULL << 7))) continue;
                {
                    uint64_t *pd = table_ptr(pdpt[j]);
                    for (k = 0; k < 512; k++) {
                        if (!(pd[k] & PAGE_PRESENT) || (pd[k] & (1ULL << 7))) continue;
                        {
                            uint64_t *pt = table_ptr(pd[k]);
                            for (l = 0; l < 512; l++) {
                                if (pt[l] & PAGE_PRESENT) {
                                    pmm_free_frame(pt[l] & PTE_ADDR_MASK);
                                }
                            }
                            pmm_free_frame(pd[k] & PTE_ADDR_MASK);
                        }
                    }
                    pmm_free_frame(pdpt[j] & PTE_ADDR_MASK);
                }
            }
            pmm_free_frame(root[i] & PTE_ADDR_MASK);
        }
    }
    pmm_free_frame(as->pml4_phys);
    kfree(as);
}

void address_space_switch(address_space_t *as)
{
    if (!as) return;
    write_cr3(as->pml4_phys);
    if (as == &kernel_as) {
        pml4 = (uint64_t *)phys_to_virt(kernel_pml4_phys);
    } else {
        pml4 = (uint64_t *)phys_to_virt(as->pml4_phys);
    }
}

int map_user_page(address_space_t *as, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *root;
    if (!as) return -1;
    if (virt >= 0x0000800000000000ULL) return -1; /* must be low canonical user */
    root = (uint64_t *)phys_to_virt(as->pml4_phys);
    return map_page_in(root, virt, phys, flags | PAGE_USER);
}

uint64_t address_space_virt_to_phys(address_space_t *as, uint64_t virt)
{
    uint64_t *root;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t pml4e = (virt >> 39) & 0x1ff;
    uint64_t pdpte = (virt >> 30) & 0x1ff;
    uint64_t pde = (virt >> 21) & 0x1ff;
    uint64_t pte = (virt >> 12) & 0x1ff;

    if (!as) return 0;
    root = (uint64_t *)phys_to_virt(as->pml4_phys);
    if (!(root[pml4e] & PAGE_PRESENT)) return 0;
    pdpt = table_ptr(root[pml4e]);
    if (!(pdpt[pdpte] & PAGE_PRESENT)) return 0;
    pd = table_ptr(pdpt[pdpte]);
    if (!(pd[pde] & PAGE_PRESENT) || (pd[pde] & (1ULL << 7))) return 0;
    pt = table_ptr(pd[pde]);
    if (!(pt[pte] & PAGE_PRESENT)) return 0;
    return (pt[pte] & PTE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
}

static int walk_present(address_space_t *as, uint64_t virt)
{
    uint64_t *root;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t pml4e = (virt >> 39) & 0x1ff;
    uint64_t pdpte = (virt >> 30) & 0x1ff;
    uint64_t pde = (virt >> 21) & 0x1ff;
    uint64_t pte = (virt >> 12) & 0x1ff;

    if (!as) return 0;
    root = (uint64_t *)phys_to_virt(as->pml4_phys);
    if (!(root[pml4e] & PAGE_PRESENT)) return 0;
    pdpt = table_ptr(root[pml4e]);
    if (!(pdpt[pdpte] & PAGE_PRESENT)) return 0;
    pd = table_ptr(pdpt[pdpte]);
    if (!(pd[pde] & PAGE_PRESENT)) return 0;
    pt = table_ptr(pd[pde]);
    return (pt[pte] & PAGE_PRESENT) && (pt[pte] & PAGE_USER) ? 1 : 0;
}

int address_space_is_user_range(address_space_t *as, uint64_t virt, uint64_t size)
{
    uint64_t addr;
    uint64_t end;
    if (!as || size == 0) return 0;
    end = virt + size;
    if (end < virt || end > 0x0000800000000000ULL) return 0;
    for (addr = virt & ~(PAGE_SIZE - 1); addr < end; addr += PAGE_SIZE) {
        if (!walk_present(as, addr)) return 0;
    }
    return 1;
}

void paging_user_probe(void)
{
    address_space_t *as;
    uint64_t phys;
    volatile uint64_t *via_hhdm;
    volatile uint64_t *via_user;
    const uint64_t user_virt = 0x0000000000100000ULL;
    uint64_t saved_cr3 = read_cr3();

    as = address_space_create();
    if (!as) panic("user as: create failed");
    phys = pmm_alloc_frame();
    if (!phys) panic("user as: frame failed");
    via_hhdm = (volatile uint64_t *)phys_to_virt(phys);
    via_hhdm[0] = 0x5553455241530001ULL;
    if (map_user_page(as, user_virt, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
        panic("user as: map failed");
    }
    address_space_switch(as);
    via_user = (volatile uint64_t *)(uintptr_t)user_virt;
    user_access_begin();
    if (via_user[0] != 0x5553455241530001ULL) {
        user_access_end();
        panic("user as: read failed");
    }
    user_access_end();
    write_cr3(saved_cr3);
    pml4 = (uint64_t *)phys_to_virt(kernel_pml4_phys);
    address_space_destroy(as);
    console_puts("user as OK\n");
}

void paging_init(const struct boot_info *boot)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint64_t cr4;

    (void)boot; /* HHDM/kernel bases live in pmm; phys_to_virt uses those. */

    clone_pml4_from_limine();
    paging_probe();

    /*
     * Enable SMEP/SMAP only when CPUID reports them. Writing unsupported CR4
     * bits #GPs on real hardware and appears as a hang right after
     * "paging probe OK" (next line never prints).
     * Leaf 7 EBX: SMEP=bit7, SMAP=bit20.
     */
    eax = 7;
    ecx = 0;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(eax), "c"(ecx)
                     : "memory");
    cr4 = read_cr4();
    if (ebx & (1U << 7)) {
        cr4 |= (1ULL << 20); /* SMEP */
    }
    if (ebx & (1U << 20)) {
        cr4 |= (1ULL << 21); /* SMAP */
    }
    write_cr4(cr4);
}
