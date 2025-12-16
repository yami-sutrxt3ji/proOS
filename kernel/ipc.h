#ifndef IPC_H
#define IPC_H

#include "config.h"
#include "ipc_types.h"

struct process;

void ipc_system_init(void);
int ipc_channel_create(const char *name, size_t name_len, uint32_t flags);
int ipc_channel_join(struct process *proc, int channel_id);
int ipc_channel_leave(struct process *proc, int channel_id);
int ipc_channel_send(int channel_id, int sender_pid, uint32_t header, uint32_t type, const void *data, size_t size, uint32_t flags);
int ipc_channel_receive(struct process *proc, int channel_id, struct ipc_message *out, void *buffer, size_t buffer_len, uint32_t flags);
int ipc_channel_peek(int channel_id);
int ipc_get_service_channel(enum ipc_service_channel service);
int ipc_is_initialized(void);
void ipc_process_cleanup(struct process *proc);

#endif
