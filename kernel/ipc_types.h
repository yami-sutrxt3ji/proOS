#ifndef IPC_TYPES_H
#define IPC_TYPES_H

#include <stddef.h>
#include <stdint.h>

enum ipc_service_channel
{
    IPC_SERVICE_DEVMGR = 0,
    IPC_SERVICE_MODULE_LOADER = 1,
    IPC_SERVICE_LOGGER = 2,
    IPC_SERVICE_SCHEDULER = 3,
    IPC_SERVICE_COUNT
};

struct ipc_message
{
    uint32_t header;
    int32_t sender_pid;
    uint32_t type;
    uint32_t size;
    void *data;
};

#define IPC_RECV_NONBLOCK 0x1u
#define IPC_MESSAGE_TRUNCATED 0x1u

#endif
