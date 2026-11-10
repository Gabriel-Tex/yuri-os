#include "client_queue.h"

#include <stddef.h>

bool client_queue_init(struct client_queue *queue)
{
    if (queue == NULL)
    {
        return false;
    }
    queue->count = 0;
    queue->reserved = false;
    return true;
}

size_t client_queue_waiting_count(const struct client_queue *queue)
{
    if (queue == NULL)
    {
        return 0;
    }
    return queue->count + (queue->reserved ? 1u : 0u);
}

bool client_queue_has_space(const struct client_queue *queue)
{
    return queue != NULL && queue->count < CLIENT_QUEUE_CAPACITY &&
           !queue->reserved;
}

bool client_queue_reserve(struct client_queue *queue)
{
    if (!client_queue_has_space(queue))
    {
        return false;
    }
    queue->reserved = true;
    return true;
}

bool client_queue_release_reservation(struct client_queue *queue)
{
    if (queue == NULL || !queue->reserved)
    {
        return false;
    }
    queue->reserved = false;
    return true;
}

bool client_queue_insert(struct client_queue *queue,
                         const struct client_record *client)
{
    if (queue == NULL || client == NULL || !queue->reserved ||
        queue->count >= CLIENT_QUEUE_CAPACITY || client->state != CLIENT_QUEUED)
    {
        return false;
    }
    queue->records[queue->count] = *client;
    queue->count++;
    queue->reserved = false;
    return true;
}

bool client_queue_take_next(struct client_queue *queue,
                            struct client_record *client)
{
    if (queue == NULL || client == NULL || queue->count == 0 ||
        queue->count > CLIENT_QUEUE_CAPACITY)
    {
        return false;
    }

    /* A busca linear limita o trabalho a 100 comparações e não descarta
     * clientes com prazo vencido: todos continuam elegíveis no modo finito. */
    size_t best = 0;
    for (size_t i = 1; i < queue->count; i++)
    {
        const struct client_record *candidate = &queue->records[i];
        const struct client_record *selected = &queue->records[best];
        if ((!candidate->deadline_carry && selected->deadline_carry) ||
            (candidate->deadline_carry == selected->deadline_carry &&
             candidate->deadline_ns < selected->deadline_ns) ||
            (candidate->deadline_carry == selected->deadline_carry &&
             candidate->deadline_ns == selected->deadline_ns &&
             candidate->sequence < selected->sequence))
        {
            best = i;
        }
    }

    *client = queue->records[best];
    queue->count--;
    queue->records[best] = queue->records[queue->count];
    return true;
}
