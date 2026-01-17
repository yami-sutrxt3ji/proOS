#include "syslib.h"

#define BUFFER_SIZE 256

extern void user_echo_service(void);

int g_echo_channel = -1;

static size_t str_len(const char *s)
{
    size_t len = 0;
    while (s[len])
        ++len;
    return len;
}

void user_init(void)
{
    int logd_pid = sys_service_connect(SYSTEM_SERVICE_LOGD, IPC_RIGHT_SEND | IPC_RIGHT_RECV);
    (void)logd_pid;

    int channel = sys_chan_create("echo", 4, 0);
    if (channel < 0)
        sys_exit(1);

    if (sys_chan_join(channel) < 0)
        sys_exit(1);

    g_echo_channel = channel;

    int echo_pid = sys_spawn(user_echo_service, 4096);
    if (echo_pid < 0)
        sys_exit(1);

    const char *text = "Hello";
    struct ipc_message message;
    message.header = 0;
    message.sender_pid = 0;
    message.type = 1;
    message.size = str_len(text);
    message.data = (void *)text;

    if (sys_chan_send(channel, &message, 0) < 0)
        sys_exit(1);

    char reply_buffer[BUFFER_SIZE];
    struct ipc_message reply;
    reply.header = 0;
    reply.sender_pid = 0;
    reply.type = 0;
    reply.size = sizeof(reply_buffer) - 1;
    reply.data = reply_buffer;

    (void)sys_chan_recv(channel, &reply, 0);

    sys_exit(0);
}
