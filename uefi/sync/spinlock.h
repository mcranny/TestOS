#ifndef TESTOS_UEFI_SYNC_SPINLOCK_H
#define TESTOS_UEFI_SYNC_SPINLOCK_H

#include "types.h"
#include "arch/io.h"

/*
 * IRQ-safe test-and-set spinlock.
 *
 * Single-core today: irqsave variants prevent interrupt re-entry deadlock.
 * Multi-core later: the atomic exchange also serializes across CPUs.
 * Prefer spin_lock_irqsave / spin_unlock_irqrestore for shared kernel data.
 * Do not claim SMP correctness from this primitive alone — callers still need
 * correct critical-section scope and per-CPU design where required.
 */

typedef struct spinlock {
    volatile uint32_t locked;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

static inline void spinlock_init(spinlock_t *lock)
{
    __atomic_store_n(&lock->locked, 0, __ATOMIC_RELAXED);
}

static inline void spin_lock(spinlock_t *lock)
{
    while (__atomic_exchange_n(&lock->locked, 1U, __ATOMIC_ACQUIRE)) {
        __asm__ volatile("pause");
    }
}

static inline void spin_unlock(spinlock_t *lock)
{
    __atomic_store_n(&lock->locked, 0U, __ATOMIC_RELEASE);
}

/* Disable IRQs, then acquire. Returns prior RFLAGS for restore. */
static inline uint64_t spin_lock_irqsave(spinlock_t *lock)
{
    uint64_t flags = irq_save();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, uint64_t flags)
{
    spin_unlock(lock);
    irq_restore(flags);
}

#endif
