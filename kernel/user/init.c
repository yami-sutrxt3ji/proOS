#include "syslib.h"

#define BUFFER_SIZE 256

extern void user_echo_service(void);

static size_t str_len(const char *s)
{
    size_t len = 0;
    while (s[len])
        ++len;
    return len;
}

static void write_line(const char *text)
{
    size_t len = str_len(text);
    if (len > 0)
        sys_write(text, len);
    sys_write("\n", 1);
}

void user_init(void)
{
    write_line("init: starting echo service");

    int echo_pid = sys_spawn(user_echo_service, 4096);
    if (echo_pid < 0)
    {
        write_line("init: spawn failed");
        sys_exit(1);
    }

    const char *message = "Hello";
    sys_send(echo_pid, message, str_len(message));

    char reply[BUFFER_SIZE];
    int from = -1;
    int received = sys_recv(reply, sizeof(reply), &from);
    if (received > 0)
    {
        sys_write(reply, (size_t)received);
        sys_write("\n", 1);
    }

    sys_exit(0);
}
