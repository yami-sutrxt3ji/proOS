#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stddef.h>

void kb_init(void);
char kb_getchar(void);
int kb_dump_layout(char *out, size_t max_len);

#define KB_IRQ_LINE 1
#define KB_EVENT_FLAG_RELEASE 0x100u

#endif
