#define _GNU_SOURCE
#include "workers.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/random.h>

struct workers_cleanup_state {
    struct service_state *service;
    struct workers_reception *reception;
    struct workers_attendant *attendant;
    pid_t pids[CLIENT_QUEUE_CAPACITY + 1];
    bool reaped[CLIENT_QUEUE_CAPACITY + 1];
    size_t count;
    size_t remaining;
    int error;
};

static bool workers_terminal(const struct processes_event *event)
{
    return event->kind == PROCESSES_EXITED_OK ||
           event->kind == PROCESSES_EXITED_ERROR ||
           event->kind == PROCESSES_SIGNALED;
}

static bool workers_queued_contains(const struct workers_reception *reception,
                                     pid_t pid)
{
    for (size_t i = 0; reception->queue != NULL &&
                       i < reception->queue->count; i++) {
        if (reception->queue->records[i].pid == pid) return true;
    }
    return false;
}

static bool workers_queued_reaped(const struct workers_reception *reception,
                                   pid_t pid)
{
    for (size_t i = 0; i < reception->reaped_queued_count; i++) {
        if (reception->reaped_queued[i] == pid) return true;
    }
    return false;
}

static bool workers_mark_queued_reaped(struct workers_reception *reception,
                                       pid_t pid)
{
    if (workers_queued_reaped(reception, pid)) return true;
    if (reception->reaped_queued_count == CLIENT_QUEUE_CAPACITY) {
        errno = EOVERFLOW;
        return false;
    }
    reception->reaped_queued[reception->reaped_queued_count++] = pid;
    return true;
}

static bool workers_mark_queued_failed(struct workers_reception *reception,
                                       pid_t pid)
{
    for (size_t i = 0; i < reception->failed_queued_count; i++) {
        if (reception->failed_queued[i] == pid) return true;
    }
    if (reception->failed_queued_count == CLIENT_QUEUE_CAPACITY) {
        errno = EOVERFLOW;
        return false;
    }
    reception->failed_queued[reception->failed_queued_count++] = pid;
    return true;
}

static void workers_cleanup_event(void *context,
                                   const struct processes_event *event)
{
    struct workers_cleanup_state *cleanup = context;
    if (event->pid == cleanup->service->analyst_pid) {
        if (event->kind == PROCESSES_EXITED_OK ||
            event->kind == PROCESSES_EXITED_ERROR ||
            event->kind == PROCESSES_SIGNALED)
            cleanup->service->analyst_reaped = true;
        if (!analyst_coordinator_observe(&cleanup->service->coordinator,
                                          event) && cleanup->error == 0)
            cleanup->error = errno;
        return;
    }
    if (!workers_terminal(event)) return;
    size_t target = cleanup->count;
    for (size_t i = 0; i < cleanup->count; i++) {
        if (cleanup->pids[i] == event->pid) {
            target = i;
            break;
        }
    }
    bool owned = false;
    if (cleanup->reception != NULL) {
        struct workers_reception *reception = cleanup->reception;
        if (reception->starting_active &&
            reception->starting.pid == event->pid) {
            reception->starting_reaped = true;
            if (target == cleanup->count) reception->starting_failed = true;
            owned = true;
        } else if (workers_queued_contains(reception, event->pid)) {
            if (!workers_mark_queued_reaped(reception, event->pid) &&
                cleanup->error == 0) cleanup->error = errno;
            if (target == cleanup->count &&
                !workers_mark_queued_failed(reception, event->pid) &&
                cleanup->error == 0) cleanup->error = errno;
            owned = true;
        }
    }
    if (cleanup->attendant != NULL && cleanup->attendant->active_valid &&
        cleanup->attendant->active.pid == event->pid) {
        cleanup->attendant->active_reaped = true;
        owned = true;
    }
    if (target < cleanup->count && !cleanup->reaped[target]) {
        cleanup->reaped[target] = true;
        cleanup->remaining--;
        return;
    }
    if (!owned && cleanup->error == 0) cleanup->error = EPROTO;
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
    return reception->error != 0 ||
           (reception->cancel_starting_on_stop &&
            *reception->stop_requested) ||
           reception->demand.stopped;
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
            if (workers_terminal(event)) {
                reception->starting_reaped = true;
                reception->starting_failed = true;
            }
            workers_fail(reception, errno);
        } else if (reception->demand.stopped) {
            scheduler_notify(&reception->service->scheduler,
                             WORKERS_START_EVENT);
        }
        return;
    }
    if (workers_queued_contains(reception, event->pid)) {
        if (workers_terminal(event) &&
            (!workers_mark_queued_reaped(reception, event->pid) ||
             !workers_mark_queued_failed(reception, event->pid))) {
            workers_fail(reception, errno);
        } else if (workers_terminal(event)) {
            workers_fail(reception, ECHILD);
        } else if (event->kind != PROCESSES_STOPPED) {
            workers_fail(reception, EPROTO);
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
    /* A Recepção estabelece o protocolo antes de liberar o Analista ou
     * criar o primeiro Cliente. A espera cede o contexto ao escalonador. */
    if (!ipc_service_create_semaphores(&reception->service->ipc) ||
        !service_start_analyst(reception->service) ||
        !scheduler_wait(&reception->service->scheduler,
                        ANALYST_COORDINATOR_EVENT, service_analyst_ready,
                        reception->service) ||
        reception->service->coordinator.state != ANALYST_COORDINATOR_STOPPED) {
        workers_fail(reception, errno != 0 ? errno : EPROTO);
        reception->finished = true;
        return;
    }
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
        if (reception->cancel_starting_on_stop &&
            *reception->stop_requested) break;
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
    struct workers_cleanup_state cleanup = {.service = reception->service,
        .reception = reception, .attendant = reception->attendant};
    if (reception->starting_active && !reception->starting_reaped) {
        cleanup.pids[cleanup.count++] = reception->starting.pid;
    }
    for (size_t i = 0; reception->queue != NULL &&
                       i < reception->queue->count; i++) {
        pid_t pid = reception->queue->records[i].pid;
        if (!workers_queued_reaped(reception, pid))
            cleanup.pids[cleanup.count++] = pid;
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
        if (reception->starting_failed) {
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
        bool failed = false;
        for (size_t i = 0; i < reception->failed_queued_count; i++) {
            if (reception->failed_queued[i] == client.pid) failed = true;
        }
        if (failed) {
            if (!metrics_record_failed(reception->metrics, &client)) return false;
        } else if (!metrics_record_cancelled(reception->metrics, &client)) {
            return false;
        }
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
    bool closed = demand_close(&reception->demand);
    if (cleanup.error != 0) {
        errno = cleanup.error;
        return false;
    }
    return closed;
}

bool workers_attendant_init(struct workers_attendant *attendant,
                            struct service_state *service,
                            struct client_queue *queue, struct metrics *metrics,
                            bool *generation_finished)
{
    if (attendant == NULL || service == NULL || !service->initialized ||
        queue == NULL || metrics == NULL || generation_finished == NULL) {
        errno = EINVAL;
        return false;
    }
    *attendant = (struct workers_attendant){.service = service, .queue = queue,
        .metrics = metrics, .generation_finished = generation_finished};
    return true;
}

static void workers_attendant_fail(struct workers_attendant *attendant,
                                    int error)
{
    attendant->error = error != 0 ? error : EIO;
    attendant->service->event_error = attendant->error;
    scheduler_notify(&attendant->service->scheduler, WORKERS_TERMINAL_EVENT);
}

static bool workers_attendant_has_work(void *context)
{
    const struct workers_attendant *attendant = context;
    return attendant->queue->count > 0 || *attendant->generation_finished ||
           attendant->error != 0 ||
           (attendant->stop_requested != NULL && *attendant->stop_requested);
}

static bool workers_attendant_terminal(void *context)
{
    const struct workers_attendant *attendant = context;
    return attendant->active_reaped || attendant->error != 0;
}

void workers_attendant_on_event(void *context,
                                const struct processes_event *event)
{
    struct workers_attendant *attendant = context;
    if (!attendant->active_valid || event->pid != attendant->active.pid) {
        workers_attendant_fail(attendant, EPROTO);
        return;
    }
    if (event->kind == PROCESSES_CONTINUED) return;
    if (event->kind == PROCESSES_STOPPED) {
        workers_attendant_fail(attendant, EPROTO);
        return;
    }
    attendant->active_reaped = true;
    if (event->kind != PROCESSES_EXITED_OK) {
        workers_attendant_fail(attendant, ECHILD);
        return;
    }
    uint64_t observed_ns;
    if (!clock_monotonic_ns(&observed_ns) ||
        !clock_elapsed_ns(attendant->service->start_ns, observed_ns,
                          &attendant->completion_ns)) {
        workers_attendant_fail(attendant, errno);
        return;
    }
    attendant->completed_ok = true;
    scheduler_notify(&attendant->service->scheduler, WORKERS_TERMINAL_EVENT);
}

void workers_attendant_run(void *argument)
{
    struct workers_attendant *attendant = argument;
    while (attendant->error == 0) {
        if (attendant->stop_requested != NULL && *attendant->stop_requested)
            break;
        if (attendant->queue->count == 0) {
            if (*attendant->generation_finished) break;
            if (!scheduler_wait(&attendant->service->scheduler,
                                WORKERS_CLIENT_EVENT,
                                workers_attendant_has_work, attendant)) {
                workers_attendant_fail(attendant, errno);
            }
            continue;
        }
        if (!client_queue_take_next(attendant->queue, &attendant->active)) {
            workers_attendant_fail(attendant, errno);
            break;
        }
        attendant->active_valid = true;
        attendant->active_reaped = false;
        attendant->completed_ok = false;
        scheduler_notify(&attendant->service->scheduler, WORKERS_SPACE_EVENT);
        if (!client_start(&attendant->active) ||
            !processes_send(attendant->active.pid, SIGCONT) ||
            !scheduler_wait(&attendant->service->scheduler,
                            WORKERS_TERMINAL_EVENT,
                            workers_attendant_terminal, attendant)) {
            workers_attendant_fail(attendant, errno);
            break;
        }
        if (attendant->error != 0 || !attendant->completed_ok) break;
        /* Saída zero e semáforo disponível são ambos necessários. A
         * tentativa posterior à saída não bloqueia a outra rotina. */
        bool available;
        if (!ipc_try_acquire(&attendant->service->ipc, IPC_ATTEND,
                             &available)) {
            workers_attendant_fail(attendant, errno);
            break;
        }
        if (!available) {
            workers_attendant_fail(attendant, EPROTO);
            break;
        }
        if (!ipc_release(&attendant->service->ipc, IPC_ATTEND)) {
            workers_attendant_fail(attendant, errno);
            break;
        }
        if (!metrics_record_completed(attendant->metrics, &attendant->active,
                                      attendant->completion_ns)) {
            workers_attendant_fail(attendant, errno);
            break;
        }
        bool appended = false;
        while (!appended && attendant->error == 0) {
            if (!analyst_coordinator_try_append(&attendant->service->coordinator,
                                                 attendant->active.pid,
                                                 &appended)) {
                workers_attendant_fail(attendant, errno);
                break;
            }
            if (!appended &&
                !scheduler_wait(&attendant->service->scheduler,
                                ANALYST_COORDINATOR_EVENT,
                                analyst_coordinator_can_append,
                                &attendant->service->coordinator)) {
                workers_attendant_fail(attendant, errno);
            }
        }
        if (attendant->error != 0) break;
        attendant->active_valid = false;
        bool woken;
        if (!analyst_coordinator_maybe_wake(&attendant->service->coordinator,
                                            attendant->queue->count == 0,
                                            &woken)) {
            workers_attendant_fail(attendant, errno);
            break;
        }
    }
    if (attendant->error == 0 &&
        !analyst_coordinator_request_drain(&attendant->service->coordinator))
        workers_attendant_fail(attendant, errno);
    if (attendant->error == 0)
        attendant->service->analyst_wake_requested = true;
    attendant->finished = attendant->error == 0;
}

bool workers_attendant_close(struct workers_attendant *attendant)
{
    if (attendant == NULL) {
        errno = EINVAL;
        return false;
    }
    int cleanup_error = 0;
    if (attendant->active_valid && !attendant->active_reaped) {
        struct workers_cleanup_state cleanup = {.service = attendant->service,
            .reception = attendant->reception, .attendant = attendant,
            .pids = {attendant->active.pid}, .count = 1, .remaining = 1};
        if (!processes_send(attendant->active.pid, SIGKILL) && errno != ESRCH)
            return false;
        while (cleanup.remaining > 0) {
            if (!processes_collect(&attendant->service->watcher,
                                   workers_cleanup_event, &cleanup, NULL) ||
                (cleanup.remaining > 0 &&
                 !processes_wait(&attendant->service->watcher))) {
                return false;
            }
        }
        attendant->active_reaped = true;
        cleanup_error = cleanup.error;
    }
    if (attendant->active_valid &&
        attendant->active.state == CLIENT_IN_SERVICE &&
        !metrics_record_failed(attendant->metrics, &attendant->active)) {
        return false;
    }
    attendant->active_valid = false;
    if (cleanup_error != 0) {
        errno = cleanup_error;
        return false;
    }
    return true;
}
