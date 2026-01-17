#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <stddef.h>

static inline uint8_t inb(uint16_t port)
{
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "dN"(port));
}

static inline void outw(uint16_t port, uint16_t value)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(value), "dN"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static inline void outl(uint16_t port, uint32_t value)
{
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "dN"(port));
}

static inline void insw(uint16_t port, void *addr, size_t count)
{
    __asm__ __volatile__("rep insw" : "=D"(addr), "=c"(count) : "d"(port), "0"(addr), "1"(count) : "memory");
}

static inline void outsw(uint16_t port, const void *addr, size_t count)
{
    __asm__ __volatile__("rep outsw" : "=S"(addr), "=c"(count) : "d"(port), "0"(addr), "1"(count));
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif
