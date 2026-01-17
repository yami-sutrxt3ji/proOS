#ifndef SERVICE_H
#define SERVICE_H

#include "service_types.h"
#include "proc.h"
#include "ipc_types.h"

void service_system_init(void);
void service_bootstrap(void);
int service_register(enum system_service service, const char *name, process_entry_t entry, uint32_t default_rights);
int service_start(enum system_service service);
pid_t service_pid(enum system_service service);
int service_grant_capabilities(pid_t requester, enum system_service service, uint32_t rights);
void service_handle_exit(pid_t pid);

#endif
