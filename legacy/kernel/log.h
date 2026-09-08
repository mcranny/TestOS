#ifndef LOG_H
#define LOG_H

#include "types.h"

/* Category debug gates: INFO/WARN/ERROR/PANIC always emit; DEBUG only if on. */
#ifndef DEBUG_MEM
#define DEBUG_MEM 0
#endif
#ifndef DEBUG_PROC
#define DEBUG_PROC 1
#endif
#ifndef DEBUG_SCHED
#define DEBUG_SCHED 0
#endif
#ifndef DEBUG_FS
#define DEBUG_FS 0
#endif
#ifndef DEBUG_ATA
#define DEBUG_ATA 0
#endif
#ifndef DEBUG_IRQ
#define DEBUG_IRQ 0
#endif
#ifndef DEBUG_SYSCALL
#define DEBUG_SYSCALL 0
#endif
#ifndef DEBUG_BOOT
#define DEBUG_BOOT 1
#endif
#ifndef DEBUG_PCI
#define DEBUG_PCI 0
#endif
#ifndef DEBUG_E1000
#define DEBUG_E1000 0
#endif
#ifndef DEBUG_NET
#define DEBUG_NET 0
#endif
#ifndef DEBUG_MOUSE
#define DEBUG_MOUSE 0
#endif

typedef enum
{
    KLOG_DEBUG,
    KLOG_INFO,
    KLOG_WARN,
    KLOG_ERROR,
    KLOG_PANIC
} klog_level_t;

void klog(klog_level_t level, const char *category, const char *message);
void klog_uint(klog_level_t level, const char *category, const char *prefix, uint32_t value);

int kernel_is_panicked(void);
void panic(const char *subsystem, const char *message) __attribute__((noreturn));

#endif
