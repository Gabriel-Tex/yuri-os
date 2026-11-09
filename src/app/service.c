#define _GNU_SOURCE
#include "service.h"
#include "workers.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVICE_RUN_SUFFIX "/run"
#define SERVICE_TEMPLATE "/execution-XXXXXX"

static void service_on_event(void *context,
                              const struct processes_event *event)
{
    struct service_state *service = context;
    if (event->pid == service->analyst_pid) {
        if (event->kind == PROCESSES_EXITED_OK ||
            event->kind == PROCESSES_EXITED_ERROR ||
            event->kind == PROCESSES_SIGNALED) {
            service->analyst_reaped = true;
        }
        if (!analyst_coordinator_observe(&service->coordinator, event) &&
            service->event_error == 0) {
            service->event_error = errno;
        }
    } else if (service->client_handler != NULL) {
        service->client_handler(service->client_context, event);
    } else if (service->event_error == 0) {
        service->event_error = EPROTO;
    }
}

bool service_process_events(void *context)
{
    struct service_state *service = context;
    if (service == NULL || !service->watcher.initialized) {
        errno = EINVAL;
        return false;
    }
    if (!processes_collect(&service->watcher, service_on_event, service, NULL)) {
        return false;
    }
    if (service->event_error != 0) {
        errno = service->event_error;
        return false;
    }
    if (service->input_enabled && !service->stop_requested) {
        bool stop = false;
        bool eof = false;
        if (!terminal_poll(&service->input, &stop, &eof)) return false;
        if (stop && !service->stop_requested) {
            service->stop_requested = true;
            scheduler_notify(&service->scheduler, WORKERS_START_EVENT);
            scheduler_notify(&service->scheduler, WORKERS_SPACE_EVENT);
            scheduler_notify(&service->scheduler, WORKERS_CLIENT_EVENT);
        }
        if (eof && !service->stop_requested) {
            errno = EPIPE;
            return false;
        }
    }
    bool woken;
    return analyst_coordinator_maybe_wake(&service->coordinator, false, &woken);
}

bool service_wait_events(void *context)
{
    struct service_state *service = context;
    if (service == NULL) {
        errno = EINVAL;
        return false;
    }
    return processes_wait_input(&service->watcher,
                                service->input_enabled && !service->stop_requested ?
                                service->input.descriptor : -1);
}

static bool service_make_directory(struct service_state *service,
                                    const char *project_root)
{
    size_t root_length = strlen(project_root);
    size_t suffix_length = sizeof(SERVICE_RUN_SUFFIX) - 1;
    size_t template_length = sizeof(SERVICE_TEMPLATE);
    if (root_length > SIZE_MAX - suffix_length - template_length) {
        errno = EOVERFLOW;
        return false;
    }
    char *run_root = malloc(root_length + suffix_length + 1);
    char *run_directory = malloc(root_length + suffix_length + template_length);
    if (run_root == NULL || run_directory == NULL) {
        free(run_root);
        free(run_directory);
        errno = ENOMEM;
        return false;
    }
    memcpy(run_root, project_root, root_length);
    memcpy(run_root + root_length, SERVICE_RUN_SUFFIX, suffix_length + 1);
    if (mkdir(run_root, 0700) == -1 && errno != EEXIST) {
        int error = errno;
        free(run_root);
        free(run_directory);
        errno = error;
        return false;
    }
    struct stat status;
    if (stat(run_root, &status) == -1) {
        int error = errno;
        free(run_root);
        free(run_directory);
        errno = error;
        return false;
    }
    if (!S_ISDIR(status.st_mode)) {
        free(run_root);
        free(run_directory);
        errno = ENOTDIR;
        return false;
    }
    memcpy(run_directory, run_root, root_length + suffix_length);
    memcpy(run_directory + root_length + suffix_length, SERVICE_TEMPLATE,
           template_length);
    free(run_root);
    if (mkdtemp(run_directory) == NULL) {
        int error = errno;
        free(run_directory);
        errno = error;
        return false;
    }
    service->run_directory = run_directory;
    return true;
}

bool service_prepare(struct service_state *service,
                     const struct service_config *config)
{
    if (service == NULL || config == NULL || service->initialized ||
        config->project_root == NULL || config->project_root[0] != '/' ||
        config->analyst_path == NULL || config->analyst_path[0] != '/' ||
        config->max_patience_ms == 0 ||
        config->max_patience_ms > UINT64_MAX / UINT64_C(1000000)) {
        errno = EINVAL;
        return false;
    }
    service->config = *config;
    if (!clock_monotonic_ns(&service->start_ns) ||
        !ipc_service_init(&service->ipc) ||
        !processes_watcher_init(&service->watcher) ||
        !service_make_directory(service, config->project_root) ||
        !completion_file_open(&service->completion, service->run_directory,
                              service->ipc.block, true) ||
        !scheduler_init(&service->scheduler, NULL, NULL, NULL)) {
        int error = errno;
        service_close(service, NULL);
        errno = error;
        return false;
    }
    char *arguments[] = {(char *)config->analyst_path,
                         service->run_directory, NULL};
    if (!processes_spawn(&service->watcher, config->analyst_path, arguments,
                         service->run_directory, &service->analyst_pid) ||
        !analyst_coordinator_init(&service->coordinator, service->analyst_pid,
                                   &service->completion, &service->scheduler)) {
        int error = errno;
        service_close(service, NULL);
        errno = error;
        return false;
    }
    /* A parada inicial é confirmada fora das rotinas de usuário. */
    while (service->coordinator.state == ANALYST_COORDINATOR_STARTING &&
           service->event_error == 0) {
        if (!service_process_events(service)) break;
        if (service->coordinator.state == ANALYST_COORDINATOR_STARTING &&
            !service_wait_events(service)) break;
    }
    if (service->coordinator.state != ANALYST_COORDINATOR_STOPPED) {
        int error = service->event_error != 0 ? service->event_error : errno;
        service_close(service, NULL);
        errno = error;
        return false;
    }
    service->initialized = true;
    return true;
}

bool service_run_finite(struct service_state *service, const char *client_path,
                        struct service_result *result)
{
    if (service == NULL || !service->initialized ||
        service->config.client_count == 0 || client_path == NULL ||
        client_path[0] != '/' || result == NULL) {
        errno = EINVAL;
        return false;
    }
    struct client_queue queue;
    struct metrics metrics;
    struct workers_reception reception = {0};
    struct workers_attendant attendant = {0};
    bool stop_requested = false;
    bool reception_ready = false;
    bool attendant_ready = false;
    bool succeeded = false;
    int error = 0;
    char *client_argv[] = {(char *)client_path, NULL};
    if (!client_queue_init(&queue) || !metrics_init(&metrics) ||
        !workers_attendant_init(&attendant, service, &queue, &metrics,
                                &reception.finished)) {
        return false;
    }
    attendant_ready = true;
    reception_ready = true;
    if (!workers_reception_init(&reception, service, &queue, &metrics,
                                client_path, client_argv, &stop_requested)) {
        error = errno;
        goto cleanup;
    }
    reception.other_handler = workers_attendant_on_event;
    reception.other_context = &attendant;
    if (!scheduler_create(&service->scheduler, 0, workers_reception_run,
                          &reception) ||
        !scheduler_create(&service->scheduler, 1, workers_attendant_run,
                          &attendant) ||
        !scheduler_run_events(&service->scheduler, service_process_events,
                              service_wait_events, service)) {
        error = errno;
        goto cleanup;
    }
    double rate;
    if (!reception.finished || !attendant.finished ||
        queue.count != 0 || queue.reserved ||
        !metrics_finite_rate(&metrics, service->config.client_count,
                             &rate)) {
        error = EPROTO;
        goto cleanup;
    }
    if (!analyst_coordinator_request_drain(&service->coordinator)) {
        error = errno;
        goto cleanup;
    }
    bool drained = false;
    while (!drained) {
        if (!service_process_events(service) ||
            !analyst_coordinator_is_drained(&service->coordinator, &drained) ||
            (!drained && !service_wait_events(service))) {
            error = errno;
            goto cleanup;
        }
    }
    succeeded = true;
cleanup:
    if (attendant_ready && !workers_attendant_close(&attendant) && error == 0)
        error = errno;
    if (reception_ready && !workers_reception_close(&reception) && error == 0)
        error = errno;
    result->created = metrics.created;
    result->completed = metrics.completed;
    result->satisfied = metrics.satisfied;
    result->cancelled = metrics.cancelled;
    result->failed = metrics.failed;
    result->complete = succeeded && error == 0;
    if (!result->complete) {
        errno = error != 0 ? error : EPROTO;
        return false;
    }
    return true;
}

bool service_run_infinite(struct service_state *service, const char *client_path,
                          struct service_result *result)
{
    if (service == NULL || !service->initialized ||
        service->config.client_count != 0 || client_path == NULL ||
        client_path[0] != '/' || result == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!terminal_init(&service->input, STDIN_FILENO)) return false;
    service->input_enabled = true;
    service->stop_requested = false;
    struct client_queue queue;
    struct metrics metrics;
    struct workers_reception reception = {0};
    struct workers_attendant attendant = {0};
    bool reception_ready = false;
    bool attendant_ready = false;
    bool succeeded = false;
    int error = 0;
    char *client_argv[] = {(char *)client_path, NULL};
    if (!client_queue_init(&queue) || !metrics_init(&metrics) ||
        !workers_attendant_init(&attendant, service, &queue, &metrics,
                                &reception.finished)) return false;
    attendant.stop_requested = &service->stop_requested;
    attendant_ready = true;
    reception_ready = true;
    if (!workers_reception_init(&reception, service, &queue, &metrics,
                                client_path, client_argv,
                                &service->stop_requested)) {
        error = errno;
        goto cleanup;
    }
    reception.cancel_starting_on_stop = true;
    reception.other_handler = workers_attendant_on_event;
    reception.other_context = &attendant;
    if (!scheduler_create(&service->scheduler, 0, workers_reception_run,
                          &reception) ||
        !scheduler_create(&service->scheduler, 1, workers_attendant_run,
                          &attendant) ||
        !scheduler_run_events(&service->scheduler, service_process_events,
                              service_wait_events, service)) {
        error = errno;
        goto cleanup;
    }
    if (!service->stop_requested || !reception.finished ||
        !attendant.finished) {
        error = EPROTO;
        goto cleanup;
    }
    /* Os registros que ainda aguardavam são cancelados antes da drenagem. */
    if (!workers_attendant_close(&attendant) ||
        !workers_reception_close(&reception)) {
        error = errno;
        goto cleanup;
    }
    attendant_ready = false;
    reception_ready = false;
    if (!analyst_coordinator_request_drain(&service->coordinator)) {
        error = errno;
        goto cleanup;
    }
    bool drained = false;
    while (!drained) {
        if (!service_process_events(service) ||
            !analyst_coordinator_is_drained(&service->coordinator, &drained) ||
            (!drained && !service_wait_events(service))) {
            error = errno;
            goto cleanup;
        }
    }
    if (metrics.created != metrics.completed + metrics.cancelled ||
        metrics.failed != 0 || queue.count != 0 || queue.reserved) {
        error = EPROTO;
        goto cleanup;
    }
    succeeded = true;
cleanup:
    if (attendant_ready && !workers_attendant_close(&attendant) && error == 0)
        error = errno;
    if (reception_ready && !workers_reception_close(&reception) && error == 0)
        error = errno;
    result->created = metrics.created;
    result->completed = metrics.completed;
    result->satisfied = metrics.satisfied;
    result->cancelled = metrics.cancelled;
    result->failed = metrics.failed;
    result->complete = succeeded && error == 0;
    if (!result->complete) {
        errno = error != 0 ? error : EPROTO;
        return false;
    }
    return true;
}

static void service_on_cleanup_event(void *context,
                                      const struct processes_event *event)
{
    struct service_state *service = context;
    if (event->pid == service->analyst_pid &&
        (event->kind == PROCESSES_EXITED_OK ||
         event->kind == PROCESSES_EXITED_ERROR ||
         event->kind == PROCESSES_SIGNALED)) {
        service->analyst_reaped = true;
    }
}

bool service_close(struct service_state *service, struct service_result *result)
{
    if (service == NULL) {
        errno = EINVAL;
        return false;
    }
    int error = 0;
    if (service->analyst_pid > 0 && !service->analyst_reaped &&
        service->watcher.initialized) {
        if (!processes_send(service->analyst_pid, SIGTERM) && errno != ESRCH) {
            error = errno;
        }
        if (!processes_send(service->analyst_pid, SIGCONT) &&
            errno != ESRCH && error == 0) {
            error = errno;
        }
        while (!service->analyst_reaped) {
            if (!processes_collect(&service->watcher,
                                   service_on_cleanup_event, service, NULL) ||
                (!service->analyst_reaped &&
                 !processes_wait(&service->watcher))) {
                if (error == 0) error = errno;
                break;
            }
        }
    }
    if (service->scheduler.initialized &&
        !scheduler_destroy(&service->scheduler) && error == 0) error = errno;
    if (service->completion.initialized &&
        !completion_file_close(&service->completion) && error == 0) error = errno;
    if ((service->input_enabled || service->input.flags_saved ||
         service->input.attributes_saved) &&
        !terminal_close(&service->input) && error == 0) error = errno;
    service->input_enabled = false;
    if (service->watcher.initialized &&
        !processes_watcher_close(&service->watcher) && error == 0) error = errno;
    if (!ipc_service_close(&service->ipc) && error == 0) error = errno;
    if (service->run_directory != NULL) {
        int directory_fd = open(service->run_directory,
                                O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory_fd != -1) {
            if (unlinkat(directory_fd, "analyst.pid", 0) == -1 &&
                errno != ENOENT && error == 0) error = errno;
            if (unlinkat(directory_fd, COMPLETION_FILE_NAME, 0) == -1 &&
                errno != ENOENT && error == 0) error = errno;
            if (unlinkat(directory_fd, DEMAND_FILE_NAME, 0) == -1 &&
                errno != ENOENT && error == 0) error = errno;
            if (close(directory_fd) == -1 && error == 0) error = errno;
        } else if (error == 0) {
            error = errno;
        }
        if (rmdir(service->run_directory) == -1 && error == 0) error = errno;
        free(service->run_directory);
        service->run_directory = NULL;
    }
    uint64_t end_ns;
    if (result != NULL) {
        if (!clock_monotonic_ns(&end_ns) ||
            !clock_elapsed_ns(service->start_ns, end_ns,
                              &result->elapsed_ns)) {
            if (error == 0) error = errno;
        } else {
            result->prepared = service->initialized;
        }
    }
    service->initialized = false;
    service->coordinator = (struct analyst_coordinator){0};
    service->analyst_pid = 0;
    service->analyst_reaped = false;
    service->event_error = 0;
    if (error != 0) {
        errno = error;
        return false;
    }
    return true;
}
