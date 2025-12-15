#include "spinlock.h"

static inline int atomic_test_and_set(volatile int *ptr)
{
    return __sync_lock_test_and_set(ptr, 1);
}

static inline void atomic_clear(volatile int *ptr)
{
    __sync_lock_release(ptr);
}

static inline void cpu_relax(void)
{
    __asm__ __volatile__("pause");
}

void spinlock_init(spinlock_t *lock)
{
    if (!lock)
        return;
    lock->locked = 0;
}

void spinlock_lock(spinlock_t *lock)
{
    if (!lock)
        return;
    while (atomic_test_and_set(&lock->locked))
    {
        for (int i = 0; i < CONFIG_STRESS_SPIN_CYCLES; ++i)
            cpu_relax();
    }
}

void spinlock_unlock(spinlock_t *lock)
{
    if (!lock)
        return;
    atomic_clear(&lock->locked);
}

void spinlock_lock_irqsave(spinlock_t *lock, uint32_t *flags)
{
    if (!lock)
        return;

    uint32_t state;
    __asm__ __volatile__("pushf; pop %0" : "=r"(state));
    __asm__ __volatile__("cli" ::: "memory");
    if (flags)
        *flags = state;
    spinlock_lock(lock);
}

void spinlock_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
    if (!lock)
        return;
    spinlock_unlock(lock);
    __asm__ __volatile__("push %0; popf" :: "r"(flags) : "memory");
}
