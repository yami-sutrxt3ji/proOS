#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>

#include "spinlock.h"

#define IRQ_MAX_LINES 16
#define IRQ_MAX_SHARED_HANDLERS 4
#define IRQ_MAX_MAILBOX_SUBSCRIBERS 4
#define IRQ_MAILBOX_CAPACITY 32

struct irq_event
{
    uint8_t irq;
    uint32_t data;
    uint32_t timestamp;
};

struct irq_mailbox
{
    struct irq_event entries[IRQ_MAILBOX_CAPACITY];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    spinlock_t lock;
};

struct regs
{
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    uint32_t int_no;
    uint32_t err_code;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
};

typedef void (*isr_callback_t)(struct regs *frame);
typedef void (*irq_callback_t)(struct regs *frame);
typedef void (*irq_shared_handler_t)(struct regs *frame, void *context);

void idt_init(void);
void isr_install_handler(int num, isr_callback_t handler);
void irq_install_handler(int irq, irq_callback_t handler);
void irq_uninstall_handler(int irq);
int irq_register_shared_handler(int irq, irq_shared_handler_t handler, void *context);
int irq_unregister_shared_handler(int irq, irq_shared_handler_t handler, void *context);

void irq_mailbox_init(struct irq_mailbox *box);
int irq_mailbox_subscribe(int irq, struct irq_mailbox *box);
int irq_mailbox_unsubscribe(int irq, struct irq_mailbox *box);
int irq_mailbox_receive(struct irq_mailbox *box, struct irq_event *out);
int irq_mailbox_peek(struct irq_mailbox *box);
void irq_mailbox_flush(struct irq_mailbox *box);

void irq_dispatch_event(int irq, uint32_t data);

#endif
