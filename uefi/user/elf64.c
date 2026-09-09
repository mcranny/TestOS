#include "user/elf64.h"
#include "mm/pmm.h"
#include "mm/paging.h"
#include "lib/string.h"
#include "platform.h"

#define EI_MAG0 0
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define EM_X86_64 62
#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define PAGE_SIZE 4096ULL
/* Low half of canonical VA space; must match map_user_page / address_space_is_user_range. */
#define USER_VA_LIMIT 0x0000800000000000ULL

struct elf64_ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

static int ensure_user_page(address_space_t *as, uint64_t page, uint64_t flags, uint8_t **dst_out)
{
    uint64_t phys;
    uint64_t map_flags = PAGE_PRESENT | PAGE_USER | (flags & (PAGE_WRITABLE | PAGE_NX));
    uint64_t *root;
    uint64_t *pdpt;
    uint64_t *pd;
    uint64_t *pt;
    uint64_t pml4e;
    uint64_t pdpte;
    uint64_t pde;
    uint64_t pte;

    /* Refuse kernel-half VAs: user AS shares kernel PML4 entries; merging
     * PAGE_USER|WRITABLE onto those PTEs would corrupt kernel mappings. */
    if (page >= USER_VA_LIMIT || (page & (PAGE_SIZE - 1)) != 0) return -1;

    phys = address_space_virt_to_phys(as, page);
    pml4e = (page >> 39) & 0x1ff;
    pdpte = (page >> 30) & 0x1ff;
    pde = (page >> 21) & 0x1ff;
    pte = (page >> 12) & 0x1ff;

    if (phys) {
        /* Merge permissions across overlapping PT_LOAD segments on one page. */
        root = (uint64_t *)phys_to_virt(as->pml4_phys);
        pdpt = (uint64_t *)phys_to_virt(root[pml4e] & 0x000ffffffffff000ULL);
        pd = (uint64_t *)phys_to_virt(pdpt[pdpte] & 0x000ffffffffff000ULL);
        pt = (uint64_t *)phys_to_virt(pd[pde] & 0x000ffffffffff000ULL);
        {
            uint64_t entry = pt[pte];
            uint64_t merged = entry | PAGE_PRESENT | PAGE_USER;
            if (map_flags & PAGE_WRITABLE) merged |= PAGE_WRITABLE;
            /* Executable if either mapping allows execute (NX clear). */
            if ((map_flags & PAGE_NX) == 0 || (entry & PAGE_NX) == 0) {
                merged &= ~PAGE_NX;
            } else {
                merged |= PAGE_NX;
            }
            pt[pte] = merged;
            __asm__ volatile("invlpg (%0)" : : "r"(page) : "memory");
        }
        *dst_out = (uint8_t *)phys_to_virt(phys & ~(PAGE_SIZE - 1));
        return 0;
    }

    phys = pmm_alloc_frame();
    if (!phys) return -1;
    *dst_out = (uint8_t *)phys_to_virt(phys);
    memset(*dst_out, 0, PAGE_SIZE);
    if (map_user_page(as, page, phys, map_flags) != 0) return -1;
    return 0;
}

int elf64_load(address_space_t *as, const void *image, uint64_t size, uint64_t *entry_out)
{
    const struct elf64_ehdr *eh;
    const struct elf64_phdr *ph;
    uint64_t ph_bytes;
    uint16_t i;

    if (!as || !image || !entry_out || size < sizeof(*eh)) return -1;
    eh = (const struct elf64_ehdr *)image;
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3) return -1;
    if (eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB) return -1;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64) return -1;
    if (eh->e_phentsize < sizeof(struct elf64_phdr)) return -1;

    /* Overflow-safe: e_phoff + phnum*entsize must lie within the image. */
    ph_bytes = (uint64_t)eh->e_phnum * (uint64_t)eh->e_phentsize;
    if (eh->e_phoff > size || ph_bytes > size - eh->e_phoff) return -1;

    /* Entry must be a user VA (jumping into kernel half is never valid). */
    if (eh->e_entry >= USER_VA_LIMIT) return -1;

    ph = (const struct elf64_phdr *)((const uint8_t *)image + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        const struct elf64_phdr *p = (const struct elf64_phdr *)((const uint8_t *)ph + i * eh->e_phentsize);
        uint64_t va, end, page, file_left, seg_end;
        const uint8_t *src;
        uint64_t flags = PAGE_PRESENT | PAGE_USER;
        if (p->p_type != PT_LOAD) continue;

        /* Overflow-safe file segment bounds. */
        if (p->p_offset > size || p->p_filesz > size - p->p_offset) return -1;
        if (p->p_filesz > p->p_memsz) return -1;

        /* Reject wrap and any range not entirely in the user half. */
        seg_end = p->p_vaddr + p->p_memsz;
        if (seg_end < p->p_vaddr) return -1;
        if (p->p_vaddr >= USER_VA_LIMIT || seg_end > USER_VA_LIMIT) return -1;
        if (p->p_memsz == 0) continue;

        if (p->p_flags & PF_W) flags |= PAGE_WRITABLE;
        if (!(p->p_flags & PF_X)) flags |= PAGE_NX;

        va = p->p_vaddr & ~(PAGE_SIZE - 1);
        /* seg_end <= USER_VA_LIMIT, so rounding up cannot wrap uint64_t. */
        end = (seg_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        src = (const uint8_t *)image + p->p_offset;
        file_left = p->p_filesz;

        for (page = va; page < end; page += PAGE_SIZE) {
            uint8_t *dst;
            uint64_t page_off = (page == va) ? (p->p_vaddr - va) : 0;
            uint64_t n;

            if (ensure_user_page(as, page, flags, &dst) != 0) return -1;

            if (file_left > 0) {
                n = PAGE_SIZE - page_off;
                if (n > file_left) n = file_left;
                memcpy(dst + page_off, src, n);
                src += n;
                file_left -= n;
            }
            /* BSS remainder stays zero from ensure_user_page / prior memset */
        }
    }

    *entry_out = eh->e_entry;
    return 0;
}
