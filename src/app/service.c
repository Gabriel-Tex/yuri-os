#define _GNU_SOURCE
#include "service.h"

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
    return processes_wait(&service->watcher);
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
            result->complete = false;
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
