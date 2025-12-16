#include "syslib.h"

#define BUFFER_SIZE 256

extern int g_echo_channel;

void user_echo_service(void)
{
    static const char prefix[] = "ECHO: ";
    const size_t prefix_len = sizeof(prefix) - 1;

    if (sys_chan_join(g_echo_channel) < 0)
        sys_exit(1);

    char inbound[BUFFER_SIZE];
    char outbound[BUFFER_SIZE];

    struct ipc_message msg;
    struct ipc_message reply;

    for (;;)
    {
        msg.header = 0;
        msg.sender_pid = 0;
        msg.type = 0;
        msg.size = sizeof(inbound) - 1;
        msg.data = inbound;

        if (sys_chan_recv(g_echo_channel, &msg, 0) <= 0)
            continue;

        size_t inbound_len = (msg.size < (sizeof(inbound) - 1)) ? msg.size : (sizeof(inbound) - 1);
        inbound[inbound_len] = '\0';

        size_t copy_len = (inbound_len < (BUFFER_SIZE - prefix_len - 1)) ? inbound_len : (BUFFER_SIZE - prefix_len - 1);
        for (size_t i = 0; i < prefix_len; ++i)
            outbound[i] = prefix[i];
        for (size_t i = 0; i < copy_len; ++i)
            outbound[prefix_len + i] = inbound[i];
        size_t total = prefix_len + copy_len;
        outbound[total] = '\0';

        reply.header = 0;
        reply.sender_pid = msg.sender_pid;
        reply.type = 1;
        reply.size = total;
        reply.data = outbound;

        sys_chan_send(g_echo_channel, &reply, 0);
    }
}
