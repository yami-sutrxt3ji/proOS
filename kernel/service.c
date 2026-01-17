/* Service manager maintains PID ownership and default IPC rights for
 * the system services defined in service_types.h. */

#include "service.h"

#include "config.h"
#include "spinlock.h"
#include "ipc.h"
#include "klog.h"

#include <stddef.h>

struct service_entry
{
    enum system_service id;
    const char *name;
    process_entry_t entry;
    pid_t pid;
    uint32_t rights_default;
    uint32_t client_rights[CONFIG_MAX_PROCS];
};

static struct service_entry service_table[SYSTEM_SERVICE_COUNT];
static spinlock_t service_lock;

static inline void zero_service_entry(struct service_entry *svc)
{
    if (!svc)
        return;
    svc->entry = NULL;
    svc->pid = -1;
    svc->rights_default = 0;
    for (size_t i = 0; i < CONFIG_MAX_PROCS; ++i)
        svc->client_rights[i] = 0;
}

static const char *service_tag(const struct service_entry *svc)
{
    if (!svc || !svc->name)
        return "service";
    return svc->name;
}

void service_system_init(void)
{
    spinlock_init(&service_lock);
    for (size_t i = 0; i < SYSTEM_SERVICE_COUNT; ++i)
    {
        service_table[i].id = (enum system_service)i;
        service_table[i].name = NULL;
        zero_service_entry(&service_table[i]);
    }
}

int service_register(enum system_service service, const char *name, process_entry_t entry, uint32_t default_rights)
{
    if (service < 0 || service >= SYSTEM_SERVICE_COUNT)
        return -1;
    if (!entry)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&service_lock, &flags);

    struct service_entry *slot = &service_table[service];
    slot->name = name;
    slot->entry = entry;
    slot->rights_default = default_rights;
    slot->pid = -1;
    for (size_t i = 0; i < CONFIG_MAX_PROCS; ++i)
        slot->client_rights[i] = 0;

    spinlock_unlock_irqrestore(&service_lock, flags);
    return 0;
}

int service_start(enum system_service service)
{
    if (service < 0 || service >= SYSTEM_SERVICE_COUNT)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&service_lock, &flags);

    struct service_entry *slot = &service_table[service];
    if (!slot->entry)
    {
        spinlock_unlock_irqrestore(&service_lock, flags);
        return -1;
    }

    if (slot->pid > 0)
    {
        spinlock_unlock_irqrestore(&service_lock, flags);
        return slot->pid;
    }

    spinlock_unlock_irqrestore(&service_lock, flags);

    int pid = process_create(slot->entry, PROC_STACK_SIZE);
    if (pid > 0)
    {
        spinlock_lock_irqsave(&service_lock, &flags);
        slot->pid = pid;
        spinlock_unlock_irqrestore(&service_lock, flags);
        klog_emit_tagged(service_tag(slot), KLOG_INFO, "service started");
    }
    else
    {
        klog_emit_tagged(service_tag(slot), KLOG_ERROR, "service spawn failed");
    }

    return pid;
}

int service_grant_capabilities(pid_t requester, enum system_service service, uint32_t rights)
{
    if (service < 0 || service >= SYSTEM_SERVICE_COUNT)
        return -1;
    if (requester <= 0)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&service_lock, &flags);

    struct service_entry *slot = &service_table[service];
    pid_t svc_pid = slot->pid;
    uint32_t effective = slot->rights_default | rights;
    spinlock_unlock_irqrestore(&service_lock, flags);

    if (svc_pid <= 0)
    {
        if (service_start(service) <= 0)
            return -1;
        svc_pid = service_pid(service);
        if (svc_pid <= 0)
            return -1;
    }

    if (ipc_cap_grant(requester, svc_pid, effective) < 0)
        return -1;
    if (ipc_cap_grant(svc_pid, requester, effective) < 0)
        return -1;

    if (requester <= CONFIG_MAX_PROCS)
    {
        spinlock_lock_irqsave(&service_lock, &flags);
        struct service_entry *slot_update = &service_table[service];
        slot_update->client_rights[(size_t)(requester - 1)] = effective;
        spinlock_unlock_irqrestore(&service_lock, flags);
    }

    return 0;
}

pid_t service_pid(enum system_service service)
{
    if (service < 0 || service >= SYSTEM_SERVICE_COUNT)
        return -1;

    uint32_t flags;
    spinlock_lock_irqsave(&service_lock, &flags);
    pid_t pid = service_table[service].pid;
    spinlock_unlock_irqrestore(&service_lock, flags);
    return pid;
}

void service_handle_exit(pid_t pid)
{
    if (pid <= 0)
        return;

    uint32_t flags;
    spinlock_lock_irqsave(&service_lock, &flags);

    for (size_t i = 0; i < SYSTEM_SERVICE_COUNT; ++i)
    {
        if (service_table[i].pid == pid)
        {
            service_table[i].pid = -1;
            break;
        }
    }

    spinlock_unlock_irqrestore(&service_lock, flags);
}

void service_bootstrap(void)
{
    for (int svc = 0; svc < SYSTEM_SERVICE_COUNT; ++svc)
        service_start((enum system_service)svc);
}
