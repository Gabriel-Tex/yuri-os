#define _GNU_SOURCE
#include "workers.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/random.h>

struct workers_cleanup_state {
    struct service_state *service;
    pid_t pids[CLIENT_QUEUE_CAPACITY + 1];
    bool reaped[CLIENT_QUEUE_CAPACITY + 1];
    size_t count;
    size_t remaining;
};

static void workers_cleanup_event(void *context,
                                   const struct processes_event *event)
{
    struct workers_cleanup_state *cleanup = context;
    if (event->kind != PROCESSES_EXITED_OK &&
        event->kind != PROCESSES_EXITED_ERROR &&
        event->kind != PROCESSES_SIGNALED) return;
    if (event->pid == cleanup->service->analyst_pid) {
        cleanup->service->analyst_reaped = true;
        return;
    }
    for (size_t i = 0; i < cleanup->count; i++) {
        if (cleanup->pids[i] == event->pid && !cleanup->reaped[i]) {
            cleanup->reaped[i] = true;
            cleanup->remaining--;
            return;
        }
    }
}

static bool workers_should_stop(void *context)
{
    const struct workers_reception *reception = context;
    return *reception->stop_requested ||
           (reception->service->config.client_count > 0 &&
            reception->metrics->created >= reception->service->config.client_count);
}

static bool workers_space_available(void *context)
{
    const struct workers_reception *reception = context;
    return workers_should_stop(context) ||
           client_queue_has_space(reception->queue);
}

static bool workers_start_observed(void *context)
{
    const struct workers_reception *reception = context;
    return reception->error != 0 || reception->demand.stopped;
}

static uint64_t workers_next_random(struct workers_reception *reception)
{
    uint64_t state = reception->random_state;
    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    reception->random_state = state;
    return state * UINT64_C(2685821657736338717);
}

enum client_priority workers_priority_from_bits(uint64_t bits)
{
    return (bits & UINT64_C(1)) == 0 ? CLIENT_PRIORITY_HIGH :
                                          CLIENT_PRIORITY_LOW;
}

bool workers_reception_init(struct workers_reception *reception,
                            struct service_state *service,
                            struct client_queue *queue, struct metrics *metrics,
                            const char *client_path, char *const client_argv[],
                            bool *stop_requested)
{
    if (reception == NULL || service == NULL || !service->initialized ||
        queue == NULL || metrics == NULL || client_path == NULL ||
        client_path[0] != '/' || client_argv == NULL ||
        client_argv[0] == NULL || stop_requested == NULL) {
        errno = EINVAL;
        return false;
    }
    uint64_t seed;
    ssize_t read_count;
    do {
        read_count = getrandom(&seed, sizeof(seed), 0);
    } while (read_count == -1 && errno == EINTR);
    if (read_count != (ssize_t)sizeof(seed)) {
        if (read_count >= 0) errno = EIO;
        return false;
    }
    if (seed == 0) seed = UINT64_C(0x9e3779b97f4a7c15);
    *reception = (struct workers_reception){.service = service,
        .queue = queue, .metrics = metrics, .client_path = client_path,
        .client_argv = client_argv, .stop_requested = stop_requested,
        .random_state = seed};
    if (!demand_init(&reception->demand, service->run_directory)) return false;
    service->client_handler = workers_reception_on_event;
    service->client_context = reception;
    return true;
}

static void workers_fail(struct workers_reception *reception, int error)
{
    reception->error = error != 0 ? error : EIO;
    reception->service->event_error = reception->error;
    scheduler_notify(&reception->service->scheduler, WORKERS_CLIENT_EVENT);
    scheduler_notify(&reception->service->scheduler, WORKERS_SPACE_EVENT);
}

void workers_reception_on_event(void *context,
                                const struct processes_event *event)
{
    struct workers_reception *reception = context;
    if (reception->starting_active && event->pid == reception->starting.pid) {
        if (!demand_observe(&reception->demand, event)) {
            if (event->kind == PROCESSES_EXITED_OK ||
                event->kind == PROCESSES_EXITED_ERROR ||
                event->kind == PROCESSES_SIGNALED) {
                reception->starting_reaped = true;
            }
            workers_fail(reception, errno);
        } else if (reception->demand.stopped) {
            scheduler_notify(&reception->service->scheduler,
                             WORKERS_START_EVENT);
        }
        return;
    }
    if (reception->other_handler != NULL) {
        reception->other_handler(reception->other_context, event);
    } else {
        workers_fail(reception, EPROTO);
    }
}

void workers_reception_run(void *argument)
{
    struct workers_reception *reception = argument;
    while (!workers_should_stop(reception) && reception->error == 0) {
        if (!client_queue_has_space(reception->queue)) {
            if (!scheduler_wait(&reception->service->scheduler,
                                WORKERS_SPACE_EVENT, workers_space_available,
                                reception)) {
                workers_fail(reception, errno);
                break;
            }
            continue;
        }
        if (!client_queue_reserve(reception->queue)) {
            workers_fail(reception, errno);
            break;
        }
        pid_t pid;
        uint64_t arrival_ns;
        if (!demand_begin(&reception->demand, &reception->service->watcher,
                          reception->client_path, reception->client_argv,
                          reception->service->run_directory,
                          reception->service->start_ns, &pid, &arrival_ns)) {
            client_queue_release_reservation(reception->queue);
            workers_fail(reception, errno);
            break;
        }
        reception->starting.pid = pid;
        reception->starting_active = true;
        reception->starting_record_valid = false;
        reception->starting_reaped = false;
        enum client_priority priority = workers_priority_from_bits(
            workers_next_random(reception) >> 63);
        if (!metrics_record_created(reception->metrics) ||
            !client_init(&reception->starting, pid, reception->next_sequence,
                         priority, arrival_ns,
                         reception->service->config.max_patience_ms)) {
            workers_fail(reception, errno);
            break;
        }
        reception->starting_record_valid = true;
        reception->next_sequence++;
        if (!scheduler_wait(&reception->service->scheduler, WORKERS_START_EVENT,
                            workers_start_observed, reception)) {
            workers_fail(reception, errno);
            break;
        }
        if (reception->error != 0) break;
        uint64_t demand_us;
        if (!demand_consume(&reception->demand, &demand_us) ||
            !client_enqueue(&reception->starting, demand_us) ||
            !client_queue_insert(reception->queue, &reception->starting)) {
            workers_fail(reception, errno);
            break;
        }
        reception->starting_active = false;
        reception->starting_record_valid = false;
        size_t waiting = client_queue_waiting_count(reception->queue);
        if (waiting > reception->maximum_waiting) {
            reception->maximum_waiting = waiting;
        }
        scheduler_notify(&reception->service->scheduler,
                         WORKERS_CLIENT_EVENT);
        if (!scheduler_yield()) {
            workers_fail(reception, errno);
            break;
        }
    }
    reception->finished = true;
    scheduler_notify(&reception->service->scheduler, WORKERS_CLIENT_EVENT);
}

bool workers_reception_close(struct workers_reception *reception)
{
    if (reception == NULL) {
        errno = EINVAL;
        return false;
    }
    struct workers_cleanup_state cleanup = {.service = reception->service};
    if (reception->starting_active && !reception->starting_reaped) {
        cleanup.pids[cleanup.count++] = reception->starting.pid;
    }
    for (size_t i = 0; reception->queue != NULL &&
                       i < reception->queue->count; i++) {
        cleanup.pids[cleanup.count++] = reception->queue->records[i].pid;
    }
    cleanup.remaining = cleanup.count;
    for (size_t i = 0; i < cleanup.count; i++) {
        if (!processes_send(cleanup.pids[i], SIGKILL) && errno != ESRCH) {
            return false;
        }
    }
    /* A coleta ocorre fora das threads de usuário; os filhos parados são
     * recolhidos antes de devolver a reserva e os registros da fila. */
    while (cleanup.remaining > 0) {
        if (!processes_collect(&reception->service->watcher,
                               workers_cleanup_event, &cleanup, NULL)) {
            return false;
        }
        if (cleanup.remaining > 0 &&
            !processes_wait(&reception->service->watcher)) return false;
    }
    reception->starting_reaped = true;
    if (reception->starting_active && reception->starting_record_valid) {
        if (reception->error != 0 && !reception->demand.stopped) {
            if (!metrics_record_failed(reception->metrics,
                                       &reception->starting)) return false;
        } else if (!metrics_record_cancelled(reception->metrics,
                                             &reception->starting)) {
            return false;
        }
    }
    while (reception->queue != NULL && reception->queue->count > 0) {
        struct client_record client;
        if (!client_queue_take_next(reception->queue, &client)) return false;
        if (!metrics_record_cancelled(reception->metrics, &client)) return false;
    }
    if (reception->queue != NULL && reception->queue->reserved) {
        if (!client_queue_release_reservation(reception->queue)) return false;
    }
    demand_abort(&reception->demand);
    reception->starting_active = false;
    if (reception->service != NULL &&
        reception->service->client_context == reception) {
        reception->service->client_context = NULL;
        reception->service->client_handler = NULL;
    }
    return demand_close(&reception->demand);
}
