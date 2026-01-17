#include "interrupts.h"
#include "pic.h"
#include "vga.h"
#include "pit.h"

#include <stdint.h>
#include <stddef.h>

struct idt_entry
{
    uint16_t base_low;
    uint16_t sel;
    uint8_t zero;
    uint8_t flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt_entries[256];
static struct idt_ptr idt_descriptor;

static isr_callback_t isr_handlers[32];

struct irq_shared_entry
{
    irq_shared_handler_t handler;
    void *context;
};

struct irq_dispatch_slot
{
    irq_callback_t primary;
    struct irq_shared_entry shared[IRQ_MAX_SHARED_HANDLERS];
    struct irq_mailbox *mailboxes[IRQ_MAX_MAILBOX_SUBSCRIBERS];
};

static struct irq_dispatch_slot irq_table[IRQ_MAX_LINES];
static spinlock_t irq_table_lock;

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void isr128(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

extern void idt_flush(uint32_t);

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt_entries[num].base_low = (uint16_t)(base & 0xFFFF);
    idt_entries[num].sel = sel;
    idt_entries[num].zero = 0;
    idt_entries[num].flags = flags;
    idt_entries[num].base_high = (uint16_t)((base >> 16) & 0xFFFF);
}

static void idt_clear(void)
{
    for (size_t i = 0; i < 256; ++i)
    {
        idt_entries[i].base_low = 0;
        idt_entries[i].sel = 0;
        idt_entries[i].zero = 0;
        idt_entries[i].flags = 0;
        idt_entries[i].base_high = 0;
    }
}

void idt_init(void)
{
    idt_clear();

    idt_descriptor.limit = (uint16_t)(sizeof(idt_entries) - 1);
    idt_descriptor.base = (uint32_t)&idt_entries;

    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0xEE);

    for (size_t i = 0; i < 32; ++i)
        isr_handlers[i] = NULL;

    spinlock_init(&irq_table_lock);
    for (size_t i = 0; i < IRQ_MAX_LINES; ++i)
    {
        irq_table[i].primary = NULL;
        for (size_t s = 0; s < IRQ_MAX_SHARED_HANDLERS; ++s)
        {
            irq_table[i].shared[s].handler = NULL;
            irq_table[i].shared[s].context = NULL;
        }
        for (size_t m = 0; m < IRQ_MAX_MAILBOX_SUBSCRIBERS; ++m)
            irq_table[i].mailboxes[m] = NULL;
    }

    idt_flush((uint32_t)&idt_descriptor);
}

static const char *exception_messages[32] = {
    "Divide-by-zero", "Debug", "Non-maskable interrupt", "Breakpoint",
    "Overflow", "Bound range", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment", "Invalid TSS", "Segment not present",
    "Stack fault", "General protection", "Page fault", "Reserved",
    "x87 floating-point", "Alignment check", "Machine check", "SIMD floating-point",
    "Virtualization", "Security", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};

static void vga_write_hex32(uint32_t value)
{
    char buffer[11];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; ++i)
    {
        uint32_t shift = (uint32_t)((7 - i) * 4);
        uint8_t nibble = (uint8_t)((value >> shift) & 0xF);
        buffer[2 + i] = (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
    }
    buffer[10] = '\0';
    vga_write_line(buffer);
}

void isr_handler(struct regs *frame)
{
    uint32_t int_no = frame->int_no;
    if (int_no < 32)
    {
        if (isr_handlers[int_no])
        {
            isr_handlers[int_no](frame);
            return;
        }

        vga_set_color(0xC, 0x0);
        vga_write_line("CPU exception!");
        if (int_no < 32)
        {
            vga_write_line(exception_messages[int_no]);
            vga_write("Vector: ");
            vga_write_hex32(int_no);
            vga_write_line("");
            vga_write("Error: ");
            vga_write_hex32(frame->err_code);
            vga_write_line("");
            vga_write("EIP: ");
            vga_write_hex32(frame->eip);
            vga_write(" CS: ");
            vga_write_hex32(frame->cs);
            vga_write_line("");
            vga_write("EFLAGS: ");
            vga_write_hex32(frame->eflags);
            vga_write_line("");
        }
        vga_write_line("System halted.");
        for (;;)
        {
            __asm__ __volatile__("hlt");
        }
    }
}

void irq_handler(struct regs *frame)
{
    uint32_t vector = frame->int_no;
    if (vector >= 32 && vector < 48)
    {
        uint8_t irq = (uint8_t)(vector - 32);
        struct irq_dispatch_slot *slot = &irq_table[irq];
        if (slot->primary)
            slot->primary(frame);

        for (size_t i = 0; i < IRQ_MAX_SHARED_HANDLERS; ++i)
        {
            if (slot->shared[i].handler)
                slot->shared[i].handler(frame, slot->shared[i].context);
        }

        pic_send_eoi(irq);
    }
}

void isr_install_handler(int num, isr_callback_t handler)
{
    if (num >= 0 && num < 32)
        isr_handlers[num] = handler;
}

void irq_install_handler(int irq, irq_callback_t handler)
{
    if (irq >= 0 && irq < 16)
    {
        uint32_t flags;
        spinlock_lock_irqsave(&irq_table_lock, &flags);
        irq_table[irq].primary = handler;
        spinlock_unlock_irqrestore(&irq_table_lock, flags);
        pic_clear_mask((uint8_t)irq);
    }
}

void irq_uninstall_handler(int irq)
{
    if (irq >= 0 && irq < 16)
    {
        uint32_t flags;
        spinlock_lock_irqsave(&irq_table_lock, &flags);
        irq_table[irq].primary = NULL;
        spinlock_unlock_irqrestore(&irq_table_lock, flags);
        pic_set_mask((uint8_t)irq);
    }
}

int irq_register_shared_handler(int irq, irq_shared_handler_t handler, void *context)
{
    if (irq < 0 || irq >= (int)IRQ_MAX_LINES || !handler)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&irq_table_lock, &flags);
    struct irq_dispatch_slot *slot = &irq_table[irq];
    for (size_t i = 0; i < IRQ_MAX_SHARED_HANDLERS; ++i)
    {
        if (slot->shared[i].handler == handler && slot->shared[i].context == context)
        {
            spinlock_unlock_irqrestore(&irq_table_lock, flags);
            return 0;
        }
    }

    for (size_t i = 0; i < IRQ_MAX_SHARED_HANDLERS; ++i)
    {
        if (!slot->shared[i].handler)
        {
            slot->shared[i].handler = handler;
            slot->shared[i].context = context;
            spinlock_unlock_irqrestore(&irq_table_lock, flags);
            pic_clear_mask((uint8_t)irq);
            return 0;
        }
    }

    spinlock_unlock_irqrestore(&irq_table_lock, flags);
    return -1;
}

int irq_unregister_shared_handler(int irq, irq_shared_handler_t handler, void *context)
{
    if (irq < 0 || irq >= (int)IRQ_MAX_LINES || !handler)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&irq_table_lock, &flags);
    struct irq_dispatch_slot *slot = &irq_table[irq];
    for (size_t i = 0; i < IRQ_MAX_SHARED_HANDLERS; ++i)
    {
        if (slot->shared[i].handler == handler && slot->shared[i].context == context)
        {
            slot->shared[i].handler = NULL;
            slot->shared[i].context = NULL;
            spinlock_unlock_irqrestore(&irq_table_lock, flags);
            return 0;
        }
    }
    spinlock_unlock_irqrestore(&irq_table_lock, flags);
    return -1;
}

void irq_mailbox_init(struct irq_mailbox *box)
{
    if (!box)
        return;
    box->head = 0;
    box->tail = 0;
    box->count = 0;
    spinlock_init(&box->lock);
}

int irq_mailbox_subscribe(int irq, struct irq_mailbox *box)
{
    if (irq < 0 || irq >= (int)IRQ_MAX_LINES || !box)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&irq_table_lock, &flags);
    struct irq_dispatch_slot *slot = &irq_table[irq];
    for (size_t i = 0; i < IRQ_MAX_MAILBOX_SUBSCRIBERS; ++i)
    {
        if (slot->mailboxes[i] == box)
        {
            spinlock_unlock_irqrestore(&irq_table_lock, flags);
            return 0;
        }
    }

    for (size_t i = 0; i < IRQ_MAX_MAILBOX_SUBSCRIBERS; ++i)
    {
        if (!slot->mailboxes[i])
        {
            slot->mailboxes[i] = box;
            spinlock_unlock_irqrestore(&irq_table_lock, flags);
            pic_clear_mask((uint8_t)irq);
            return 0;
        }
    }

    spinlock_unlock_irqrestore(&irq_table_lock, flags);
    return -1;
}

int irq_mailbox_unsubscribe(int irq, struct irq_mailbox *box)
{
    if (irq < 0 || irq >= (int)IRQ_MAX_LINES || !box)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&irq_table_lock, &flags);
    struct irq_dispatch_slot *slot = &irq_table[irq];
    for (size_t i = 0; i < IRQ_MAX_MAILBOX_SUBSCRIBERS; ++i)
    {
        if (slot->mailboxes[i] == box)
        {
            slot->mailboxes[i] = NULL;
            spinlock_unlock_irqrestore(&irq_table_lock, flags);
            return 0;
        }
    }

    spinlock_unlock_irqrestore(&irq_table_lock, flags);
    return -1;
}

static void mailbox_push(struct irq_mailbox *box, const struct irq_event *event)
{
    if (!box || !event)
        return;

    spinlock_lock(&box->lock);
    if (box->count >= IRQ_MAILBOX_CAPACITY)
    {
        box->head = (uint8_t)((box->head + 1) % IRQ_MAILBOX_CAPACITY);
        --box->count;
    }

    box->entries[box->tail] = *event;
    box->tail = (uint8_t)((box->tail + 1) % IRQ_MAILBOX_CAPACITY);
    ++box->count;
    spinlock_unlock(&box->lock);
}

int irq_mailbox_receive(struct irq_mailbox *box, struct irq_event *out)
{
    if (!box || !out)
        return 0;

    int result = 0;
    spinlock_lock(&box->lock);
    if (box->count > 0)
    {
        *out = box->entries[box->head];
        box->head = (uint8_t)((box->head + 1) % IRQ_MAILBOX_CAPACITY);
        --box->count;
        result = 1;
    }
    spinlock_unlock(&box->lock);
    return result;
}

int irq_mailbox_peek(struct irq_mailbox *box)
{
    if (!box)
        return 0;

    int count;
    spinlock_lock(&box->lock);
    count = box->count;
    spinlock_unlock(&box->lock);
    return count;
}

void irq_mailbox_flush(struct irq_mailbox *box)
{
    if (!box)
        return;
    spinlock_lock(&box->lock);
    box->head = 0;
    box->tail = 0;
    box->count = 0;
    spinlock_unlock(&box->lock);
}

void irq_dispatch_event(int irq, uint32_t data)
{
    if (irq < 0 || irq >= (int)IRQ_MAX_LINES)
        return;

    struct irq_mailbox *targets[IRQ_MAX_MAILBOX_SUBSCRIBERS];
    for (size_t i = 0; i < IRQ_MAX_MAILBOX_SUBSCRIBERS; ++i)
        targets[i] = NULL;

    spinlock_lock(&irq_table_lock);
    for (size_t i = 0; i < IRQ_MAX_MAILBOX_SUBSCRIBERS; ++i)
        targets[i] = irq_table[irq].mailboxes[i];
    spinlock_unlock(&irq_table_lock);

    struct irq_event event;
    event.irq = (uint8_t)irq;
    event.data = data;
    event.timestamp = (uint32_t)get_ticks();

    for (size_t i = 0; i < IRQ_MAX_MAILBOX_SUBSCRIBERS; ++i)
    {
        if (targets[i])
            mailbox_push(targets[i], &event);
    }
}
