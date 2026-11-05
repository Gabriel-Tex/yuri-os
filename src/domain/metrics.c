#include "metrics.h"

#include <stddef.h>

static bool metrics_has_unfinished(const struct metrics *metrics)
{
    if (metrics->completed > metrics->created ||
        metrics->cancelled > metrics->created - metrics->completed ||
        metrics->failed > metrics->created - metrics->completed - metrics->cancelled)
    {
        return false;
    }
    return metrics->created - metrics->completed - metrics->cancelled - metrics->failed > 0;
}

bool metrics_init(struct metrics *metrics)
{
    if (metrics == NULL)
    {
        return false;
    }
    *metrics = (struct metrics){0};
    return true;
}

bool metrics_record_created(struct metrics *metrics)
{
    if (metrics == NULL || metrics->created == UINT64_MAX)
    {
        return false;
    }
    metrics->created++;
    return true;
}

bool metrics_record_completed(struct metrics *metrics, struct client_record *client,
                              uint64_t completion_ns)
{
    if (metrics == NULL || client == NULL || !metrics_has_unfinished(metrics) ||
        metrics->completed == UINT64_MAX || metrics->satisfied == UINT64_MAX ||
        client->state != CLIENT_IN_SERVICE || completion_ns < client->arrival_ns)
    {
        return false;
    }

    /* Resposta inclui toda a espera e o serviço desde a chegada registrada. */
    bool satisfied = completion_ns - client->arrival_ns <= client->patience_ns;
    if (!client_complete(client, completion_ns))
    {
        return false;
    }
    metrics->completed++;
    if (satisfied)
    {
        metrics->satisfied++;
    }
    return true;
}

bool metrics_record_cancelled(struct metrics *metrics, struct client_record *client)
{
    if (metrics == NULL || client == NULL || !metrics_has_unfinished(metrics) ||
        metrics->cancelled == UINT64_MAX ||
        (client->state != CLIENT_STARTING && client->state != CLIENT_QUEUED))
    {
        return false;
    }
    if (!client_cancel(client))
    {
        return false;
    }
    metrics->cancelled++;
    return true;
}

bool metrics_record_failed(struct metrics *metrics, struct client_record *client)
{
    if (metrics == NULL || client == NULL || !metrics_has_unfinished(metrics) ||
        metrics->failed == UINT64_MAX ||
        (client->state != CLIENT_STARTING && client->state != CLIENT_QUEUED &&
         client->state != CLIENT_IN_SERVICE))
    {
        return false;
    }
    if (!client_fail(client))
    {
        return false;
    }
    metrics->failed++;
    return true;
}

bool metrics_finite_rate(const struct metrics *metrics, uint64_t total_clients,
                         double *rate)
{
    if (metrics == NULL || rate == NULL || total_clients == 0 ||
        metrics->created != total_clients || metrics->completed != total_clients ||
        metrics->cancelled != 0 || metrics->failed != 0 ||
        metrics->satisfied > metrics->completed)
    {
        return false;
    }
    *rate = (double)metrics->satisfied / (double)total_clients;
    return true;
}

bool metrics_infinite_rate(const struct metrics *metrics, double *rate)
{
    if (metrics == NULL || rate == NULL || metrics->completed == 0 ||
        metrics->satisfied > metrics->completed)
    {
        return false;
    }
    *rate = (double)metrics->satisfied / (double)metrics->completed;
    return true;
}
