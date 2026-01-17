#include "syslib.h"
#include "../ipc_types.h"
#include "../config.h"
struct logger_event
{
	uint32_t seq;
	uint8_t level;
	uint8_t reserved0;
	uint8_t reserved1;
	uint8_t reserved2;
	char module[CONFIG_KLOG_MODULE_NAME_LEN];
	char text[CONFIG_KLOG_ENTRY_LEN];
};

void user_logd(void)
{
	struct logger_event event;

	for (;;)
	{
		int rc = sys_ipc_recv(IPC_ANY_PROCESS, &event, sizeof(event));
		if (rc <= 0)
			sys_sleep(1);
	}
}

