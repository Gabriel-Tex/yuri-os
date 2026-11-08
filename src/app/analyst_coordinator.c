#define _GNU_SOURCE
#include "analyst_coordinator.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ANALYST_PID_FILE "analyst.pid"

static bool analyst_coordinator_check_pid(struct analyst_coordinator *coordinator)
{
    int descriptor = openat(coordinator->file->directory_fd, ANALYST_PID_FILE,
                            O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor == -1) return false;
    char actual[32];
    size_t used = 0;
    int error = 0;
    while (used < sizeof(actual)) {
        ssize_t count = read(descriptor, actual + used, sizeof(actual) - used);
        if (count == -1 && errno == EINTR) continue;
        if (count == -1) {
            error = errno;
            break;
        }
        if (count == 0) break;
        used += (size_t)count;
    }
    if (close(descriptor) == -1 && error == 0) error = errno;
    if (error != 0) {
        errno = error;
        return false;
    }
    char expected[32];
    int length = snprintf(expected, sizeof(expected), "%ld\n",
                          (long)coordinator->pid);
    if (length <= 0 || (size_t)length >= sizeof(expected) ||
        used != (size_t)length || memcmp(actual, expected, used) != 0) {
        errno = EPROTO;
        return false;
    }
    return true;
}

static bool analyst_coordinator_count(struct analyst_coordinator *coordinator,
                                      size_t *count)
{
    bool acquired;
    if (!completion_file_try_lock(coordinator->file, &acquired)) return false;
    if (!acquired) {
        errno = EAGAIN;
        return false;
    }
    bool counted = completion_file_count(coordinator->file, count);
    int error = errno;
    if (!completion_file_unlock(coordinator->file)) return false;
    if (!counted) {
        errno = error;
        return false;
    }
    return true;
}

bool analyst_coordinator_init(struct analyst_coordinator *coordinator, pid_t pid,
                              struct completion_file *file,
                              struct scheduler *scheduler)
{
    if (coordinator == NULL || coordinator->initialized || pid <= 0 ||
        file == NULL || !file->initialized || scheduler == NULL ||
        !scheduler->initialized) {
        errno = EINVAL;
        return false;
    }
    *coordinator = (struct analyst_coordinator){.pid = pid, .file = file,
        .scheduler = scheduler, .state = ANALYST_COORDINATOR_STARTING,
        .initialized = true};
    return true;
}

bool analyst_coordinator_observe(struct analyst_coordinator *coordinator,
                                 const struct processes_event *event)
{
    if (coordinator == NULL || !coordinator->initialized || event == NULL ||
        event->pid != coordinator->pid) {
        errno = EINVAL;
        return false;
    }
    if (event->kind == PROCESSES_EXITED_OK ||
        event->kind == PROCESSES_EXITED_ERROR ||
        event->kind == PROCESSES_SIGNALED) {
        coordinator->state = ANALYST_COORDINATOR_DEAD;
        scheduler_notify(coordinator->scheduler, ANALYST_COORDINATOR_EVENT);
        errno = ECHILD;
        return false;
    }
    if (event->kind == PROCESSES_CONTINUED &&
        coordinator->state == ANALYST_COORDINATOR_ACTIVE) {
        return true;
    }
    if (event->kind != PROCESSES_STOPPED || event->code != SIGSTOP) {
        errno = EPROTO;
        return false;
    }
    if (coordinator->state == ANALYST_COORDINATOR_STARTING) {
        if (!analyst_coordinator_check_pid(coordinator) ||
            !analyst_coordinator_count(coordinator, &coordinator->pending)) {
            return false;
        }
    } else if (coordinator->state == ANALYST_COORDINATOR_ACTIVE) {
        size_t remaining;
        if (!analyst_coordinator_count(coordinator, &remaining)) return false;
        if (coordinator->pending < coordinator->expected_printed ||
            remaining != coordinator->pending - coordinator->expected_printed) {
            errno = EPROTO;
            return false;
        }
        coordinator->pending = remaining;
        coordinator->expected_printed = 0;
    } else {
        errno = EPROTO;
        return false;
    }
    coordinator->state = ANALYST_COORDINATOR_STOPPED;
    return scheduler_notify(coordinator->scheduler,
                            ANALYST_COORDINATOR_EVENT) >= 0;
}

bool analyst_coordinator_can_append(void *context)
{
    const struct analyst_coordinator *coordinator = context;
    return coordinator != NULL && coordinator->initialized &&
           coordinator->state == ANALYST_COORDINATOR_STOPPED;
}

bool analyst_coordinator_try_append(struct analyst_coordinator *coordinator,
                                    pid_t pid, bool *appended)
{
    if (coordinator == NULL || !coordinator->initialized || pid <= 0 ||
        appended == NULL || coordinator->state == ANALYST_COORDINATOR_DEAD) {
        errno = EINVAL;
        return false;
    }
    if (!analyst_coordinator_can_append(coordinator)) {
        *appended = false;
        return true;
    }
    if (coordinator->pending == SIZE_MAX) {
        errno = EOVERFLOW;
        return false;
    }
    bool acquired;
    if (!completion_file_try_lock(coordinator->file, &acquired)) return false;
    if (!acquired) {
        errno = EAGAIN;
        return false;
    }
    bool written = completion_file_append(coordinator->file, pid);
    int error = errno;
    if (!completion_file_unlock(coordinator->file)) return false;
    if (!written) {
        errno = error;
        return false;
    }
    coordinator->pending++;
    *appended = true;
    return true;
}

bool analyst_coordinator_maybe_wake(struct analyst_coordinator *coordinator,
                                    bool idle, bool *woken)
{
    if (coordinator == NULL || !coordinator->initialized || woken == NULL ||
        coordinator->state == ANALYST_COORDINATOR_DEAD) {
        errno = EINVAL;
        return false;
    }
    *woken = false;
    if (coordinator->state != ANALYST_COORDINATOR_STOPPED ||
        coordinator->pending == 0 ||
        (coordinator->pending < COMPLETION_BATCH_LIMIT && !idle &&
         !coordinator->draining)) {
        return true;
    }
    size_t actual;
    if (!analyst_coordinator_count(coordinator, &actual)) return false;
    if (actual != coordinator->pending) {
        errno = EPROTO;
        return false;
    }
    if (!processes_send(coordinator->pid, SIGCONT)) return false;
    coordinator->expected_printed = actual < COMPLETION_BATCH_LIMIT ? actual :
                                    COMPLETION_BATCH_LIMIT;
    coordinator->state = ANALYST_COORDINATOR_ACTIVE;
    *woken = true;
    return true;
}

bool analyst_coordinator_request_drain(struct analyst_coordinator *coordinator)
{
    if (coordinator == NULL || !coordinator->initialized ||
        coordinator->state == ANALYST_COORDINATOR_DEAD) {
        errno = EINVAL;
        return false;
    }
    coordinator->draining = true;
    return true;
}

bool analyst_coordinator_is_drained(struct analyst_coordinator *coordinator,
                                    bool *drained)
{
    if (coordinator == NULL || !coordinator->initialized || drained == NULL ||
        coordinator->state == ANALYST_COORDINATOR_DEAD) {
        errno = EINVAL;
        return false;
    }
    *drained = false;
    if (!coordinator->draining ||
        coordinator->state != ANALYST_COORDINATOR_STOPPED ||
        coordinator->pending != 0) {
        return true;
    }
    size_t actual;
    if (!analyst_coordinator_count(coordinator, &actual)) return false;
    if (actual != 0) {
        errno = EPROTO;
        return false;
    }
    *drained = true;
    return true;
}
