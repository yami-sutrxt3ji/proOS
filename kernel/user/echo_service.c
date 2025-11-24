#include "syslib.h"

#define BUFFER_SIZE 256

void user_echo_service(void)
{
    static const char prefix[] = "ECHO: ";
    const size_t prefix_len = sizeof(prefix) - 1;

    char buffer[BUFFER_SIZE];
    char reply[BUFFER_SIZE];

    for (;;)
    {
        int from = -1;
        int received = sys_recv(buffer, sizeof(buffer), &from);
        if (received <= 0)
            continue;

        size_t copy_len = (received < (int)(BUFFER_SIZE - prefix_len - 1)) ? (size_t)received : (size_t)(BUFFER_SIZE - prefix_len - 1);

        for (size_t i = 0; i < prefix_len; ++i)
            reply[i] = prefix[i];
        for (size_t i = 0; i < copy_len; ++i)
            reply[prefix_len + i] = buffer[i];

        size_t total = prefix_len + copy_len;
        reply[total] = '\0';

        sys_send(from, reply, total);
    }
}
