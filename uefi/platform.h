#ifndef TESTOS_UEFI_PLATFORM_H
#define TESTOS_UEFI_PLATFORM_H

#include "types.h"
#include "arch/io.h"

void log_info(const char *msg);
void log_warn(const char *msg);
void log_error(const char *msg);
void log_hex64(const char *prefix, uint64_t value);
void panic(const char *msg) __attribute__((noreturn));

/* Compatibility logging for ported 32-bit networking code. */
#ifndef DEBUG_NET
#define DEBUG_NET 0
#endif

typedef enum {
    KLOG_DEBUG,
    KLOG_INFO,
    KLOG_WARN,
    KLOG_ERROR,
    KLOG_PANIC
} klog_level_t;

void klog(klog_level_t level, const char *category, const char *message);
void klog_uint(klog_level_t level, const char *category, const char *prefix, uint32_t value);

void *phys_to_virt(uint64_t phys);
uint64_t virt_to_phys(const void *virt);

uint64_t pmm_alloc_frame(void);
uint64_t pmm_alloc_contiguous(uint64_t frames);
void pmm_free_frame(uint64_t phys);
uint64_t pmm_total_frames(void);
uint64_t pmm_free_frames(void);

#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER     (1ULL << 2)
#define PAGE_PCD      (1ULL << 4)
#define PAGE_NX       (1ULL << 63)

int map_page(uint64_t virt, uint64_t phys, uint64_t flags);
int unmap_page(uint64_t virt);
void *map_mmio(uint64_t phys, uint64_t size);
static inline void *paging_map_mmio(uint64_t phys, uint64_t size)
{
    return map_mmio(phys, size);
}

void irq_register(uint8_t irq, void (*handler)(void *));

uint64_t timer_ticks(void);
uint64_t timer_hz(void);

/* Compatibility aliases for 32-bit driver ports. */
static inline uint64_t timer_get_ticks(void)
{
    return timer_ticks();
}

#define TIMER_FREQUENCY ((uint32_t)timer_hz())

void pic_unmask(uint8_t irq);
void pic_eoi(uint8_t irq);
#define pic_unmask_irq pic_unmask
#define pic_send_eoi pic_eoi

#endif
