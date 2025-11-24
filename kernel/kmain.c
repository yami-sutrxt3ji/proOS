#include "vga.h"
#include "ramfs.h"
#include "interrupts.h"
#include "pic.h"
#include "pit.h"
#include "keyboard.h"
#include "proc.h"
#include "syscall.h"

extern void shell_run(void);
extern void user_init(void);

static void print_banner(void)
{
    vga_set_color(0xF, 0x0);
    vga_write_line("proOS — PSEK (Protected Mode)");
    vga_set_color(0xA, 0x0);
    vga_write_line("Welcome to Phase 3");
    vga_set_color(0x7, 0x0);
    vga_write_line("Type 'help' to list commands.");
    vga_write_char('\n');
}

void kmain(void)
{
    vga_init();
    vga_clear();
    ramfs_init();
    idt_init();
    pic_init();
    pit_init(100);
    kb_init();
    process_system_init();
    syscall_init();
    if (process_create(user_init, PROC_STACK_SIZE) < 0)
        vga_write_line("init process failed");
    print_banner();
    __asm__ __volatile__("sti");
    process_schedule();
    shell_run();

    for (;;)
    {
        __asm__ __volatile__("hlt");
    }
}
