#include "client.h"

#include <errno.h>
#include <stddef.h>

bool client_init(struct client_record *client, pid_t pid, uint64_t sequence,
                 enum client_priority priority, uint64_t arrival_ns,
                 uint64_t max_patience_ms)
{
    if (client == NULL || pid <= 0 || max_patience_ms == 0 ||
        (priority != CLIENT_PRIORITY_HIGH && priority != CLIENT_PRIORITY_LOW) ||
        max_patience_ms > CLIENT_MAX_PATIENCE_MS)
    {
        errno = EINVAL;
        return false;
    }

    /* Converter antes de dividir preserva X/2 para X ímpar, sem ponto flutuante. */
    uint64_t patience_ns = max_patience_ms * CLIENT_NS_PER_MS;
    if (priority == CLIENT_PRIORITY_HIGH)
    {
        patience_ns /= 2;
    }
    *client = (struct client_record){
        .pid = pid,
        .sequence = sequence,
        .priority = priority,
        .arrival_ns = arrival_ns,
        .patience_ns = patience_ns,
        .deadline_ns = arrival_ns + patience_ns,
        .deadline_carry = arrival_ns > UINT64_MAX - patience_ns,
        .demand_us = 0,
        .state = CLIENT_STARTING,
        .completion_ns = 0};
    return true;
}

bool client_enqueue(struct client_record *client, uint64_t demand_us)
{
    if (client == NULL || client->state != CLIENT_STARTING)
    {
        return false;
    }

    client->demand_us = demand_us;
    client->state = CLIENT_QUEUED;
    return true;
}

bool client_start(struct client_record *client)
{
    if (client == NULL || client->state != CLIENT_QUEUED)
    {
        return false;
    }

    client->state = CLIENT_IN_SERVICE;
    return true;
}

bool client_complete(struct client_record *client, uint64_t completion_ns)
{
    if (client == NULL || client->state != CLIENT_IN_SERVICE ||
        completion_ns < client->arrival_ns)
    {
        return false;
    }

    client->completion_ns = completion_ns;
    client->state = CLIENT_COMPLETED;
    return true;
}

bool client_cancel(struct client_record *client)
{
    if (client == NULL || (client->state != CLIENT_STARTING &&
                           client->state != CLIENT_QUEUED))
    {
        return false;
    }

    client->state = CLIENT_CANCELLED;
    return true;
}

bool client_fail(struct client_record *client)
{
    if (client == NULL || (client->state != CLIENT_STARTING &&
                           client->state != CLIENT_QUEUED &&
                           client->state != CLIENT_IN_SERVICE))
    {
        return false;
    }

    client->state = CLIENT_FAILED;
    return true;
}
