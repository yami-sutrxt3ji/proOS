#include "syslib.h"
#include "../service_types.h"
#include "../ipc_types.h"

void user_netd(void)
{
    for (;;)
    {
        /* TODO: handle networking workloads */
        sys_sleep(10);
    }
}
