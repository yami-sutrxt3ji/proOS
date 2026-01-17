#include "syslib.h"
#include "../service_types.h"
#include "../ipc_types.h"

void user_fsd(void)
{
    for (;;)
    {
        /* TODO: handle filesystem requests via IPC */
        sys_sleep(10);
    }
}
