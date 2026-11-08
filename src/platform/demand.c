#define _GNU_SOURCE
#include "demand.h"

#include "clock.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEMAND_MAX_BYTES 32

bool demand_init(struct demand_startup *startup, const char *directory)
{
    if (startup == NULL || startup->initialized ||
        directory == NULL || directory[0] != '/') {
        errno = EINVAL;
        return false;
    }
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd == -1) {
        return false;
    }
    *startup = (struct demand_startup){.directory_fd = fd, .initialized = true};
    return true;
}

bool demand_close(struct demand_startup *startup)
{
    if (startup == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!startup->initialized) {
        return true;
    }
    int fd = startup->directory_fd;
    startup->initialized = false;
    demand_abort(startup);
    return close(fd) == 0;
}

bool demand_begin(struct demand_startup *startup,
                  struct processes_watcher *watcher, const char *path,
                  char *const argv[], const char *directory,
                  uint64_t execution_start_ns, pid_t *pid_out,
                  uint64_t *arrival_ns_out)
{
    if (startup == NULL || !startup->initialized || startup->active ||
        watcher == NULL || path == NULL || argv == NULL || directory == NULL ||
        pid_out == NULL || arrival_ns_out == NULL) {
        errno = EINVAL;
        return false;
    }
    struct stat file_status;
    struct stat directory_status;
    if (stat(directory, &directory_status) == -1 ||
        fstat(startup->directory_fd, &file_status) == -1) {
        return false;
    }
    if (directory_status.st_dev != file_status.st_dev ||
        directory_status.st_ino != file_status.st_ino) {
        errno = EINVAL;
        return false;
    }
    if (fstatat(startup->directory_fd, DEMAND_FILE_NAME, &file_status,
                AT_SYMLINK_NOFOLLOW) == 0) {
        errno = EEXIST;
        return false;
    }
    if (errno != ENOENT) {
        return false;
    }
    uint64_t now_ns;
    uint64_t arrival_ns;
    if (!clock_monotonic_ns(&now_ns) ||
        !clock_elapsed_ns(execution_start_ns, now_ns, &arrival_ns)) {
        return false;
    }
    pid_t pid;
    if (!processes_spawn(watcher, path, argv, directory, &pid)) {
        return false;
    }
    startup->pid = pid;
    startup->arrival_ns = arrival_ns;
    startup->active = true;
    startup->stopped = false;
    *pid_out = pid;
    *arrival_ns_out = arrival_ns;
    return true;
}

bool demand_observe(struct demand_startup *startup,
                    const struct processes_event *event)
{
    if (startup == NULL || event == NULL || !startup->active ||
        event->pid != startup->pid) {
        errno = EINVAL;
        return false;
    }
    if (event->kind == PROCESSES_STOPPED && event->code == SIGSTOP &&
        !startup->stopped) {
        startup->stopped = true;
        return true;
    }
    if (event->kind == PROCESSES_EXITED_OK ||
        event->kind == PROCESSES_EXITED_ERROR ||
        event->kind == PROCESSES_SIGNALED) {
        demand_abort(startup);
        errno = ECHILD;
        return false;
    }
    errno = EPROTO;
    return false;
}

bool demand_consume(struct demand_startup *startup, uint64_t *demand_us)
{
    if (startup == NULL || !startup->initialized || !startup->active ||
        !startup->stopped || demand_us == NULL) {
        errno = EINVAL;
        return false;
    }
    int fd = openat(startup->directory_fd, DEMAND_FILE_NAME,
                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1) {
        return false;
    }
    struct stat status;
    int saved_errno = 0;
    if (fstat(fd, &status) == -1) {
        saved_errno = errno;
    } else if (!S_ISREG(status.st_mode)) {
        saved_errno = EINVAL;
    }
    char buffer[DEMAND_MAX_BYTES + 1];
    size_t used = 0;
    while (saved_errno == 0 && used < sizeof(buffer)) {
        ssize_t count = read(fd, buffer + used, sizeof(buffer) - used);
        if (count == -1 && errno == EINTR) {
            continue;
        }
        if (count == -1) {
            saved_errno = errno;
            break;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }
    if (close(fd) == -1 && saved_errno == 0) {
        saved_errno = errno;
    }
    if (saved_errno != 0) {
        errno = saved_errno;
        return false;
    }
    size_t digits = used;
    if (digits > 0 && buffer[digits - 1] == '\n') {
        digits--;
    }
    if (used == sizeof(buffer) || digits == 0) {
        errno = EINVAL;
        return false;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < digits; i++) {
        if (buffer[i] < '0' || buffer[i] > '9') {
            errno = EINVAL;
            return false;
        }
        unsigned digit = (unsigned)(buffer[i] - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            errno = ERANGE;
            return false;
        }
        value = value * 10 + digit;
    }
    if (unlinkat(startup->directory_fd, DEMAND_FILE_NAME, 0) == -1) {
        return false;
    }
    *demand_us = value;
    demand_abort(startup);
    return true;
}

void demand_abort(struct demand_startup *startup)
{
    if (startup != NULL) {
        startup->pid = 0;
        startup->arrival_ns = 0;
        startup->active = false;
        startup->stopped = false;
    }
}
