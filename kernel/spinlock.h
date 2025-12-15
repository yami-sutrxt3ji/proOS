#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

#include "config.h"

typedef struct
{
    volatile int locked;
} spinlock_t;

void spinlock_init(spinlock_t *lock);
void spinlock_lock(spinlock_t *lock);
void spinlock_unlock(spinlock_t *lock);
void spinlock_lock_irqsave(spinlock_t *lock, uint32_t *flags);
void spinlock_unlock_irqrestore(spinlock_t *lock, uint32_t flags);

#endif
