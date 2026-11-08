#define _GNU_SOURCE
#include "platform/completion_file.h"
#include "platform/ipc.h"
#include "platform/processes.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: falhou: %s (errno %d)\n", \
            __FILE__, __LINE__, #condition, errno); exit(EXIT_FAILURE); \
} } while (0)

struct event_log {
    struct processes_event events[16];
    size_t count;
};

static void record_event(void *context, const struct processes_event *event)
{
    struct event_log *log = context;
    CHECK(log->count < 16);
    log->events[log->count++] = *event;
}

static struct processes_event wait_event(struct processes_watcher *watcher,
                                         pid_t pid,
                                         enum processes_event_kind kind)
{
    struct event_log log = {0};
    for (;;) {
        CHECK(processes_collect(watcher, record_event, &log, NULL));
        for (size_t i = 0; i < log.count; i++) {
            if (log.events[i].pid == pid && log.events[i].kind == kind) {
                return log.events[i];
            }
            if (log.events[i].pid == pid &&
                (log.events[i].kind == PROCESSES_EXITED_OK ||
                 log.events[i].kind == PROCESSES_EXITED_ERROR ||
                 log.events[i].kind == PROCESSES_SIGNALED)) {
                CHECK(kind == log.events[i].kind);
            }
        }
        log.count = 0;
        CHECK(processes_wait(watcher));
    }
}

static pid_t spawn_analyst(struct processes_watcher *watcher,
                           const char *path, const char *directory,
                           int output_fd)
{
    int saved_stdout = dup(STDOUT_FILENO);
    CHECK(saved_stdout >= 0);
    CHECK(dup2(output_fd, STDOUT_FILENO) == STDOUT_FILENO);
    char *arguments[] = {(char *)path, (char *)directory, NULL};
    pid_t pid = -1;
    bool spawned = processes_spawn(watcher, path, arguments, directory, &pid);
    int spawn_error = errno;
    CHECK(dup2(saved_stdout, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(saved_stdout) == 0);
    errno = spawn_error;
    CHECK(spawned && pid > 0);
    return pid;
}

static void append_range(struct completion_file *file, int first, int last)
{
    bool acquired;
    CHECK(completion_file_try_lock(file, &acquired) && acquired);
    for (int pid = first; pid <= last; pid++) {
        CHECK(completion_file_append(file, (pid_t)pid));
    }
    CHECK(completion_file_unlock(file));
}

static void check_count(struct completion_file *file, size_t expected)
{
    bool acquired;
    CHECK(completion_file_try_lock(file, &acquired) && acquired);
    size_t count;
    CHECK(completion_file_count(file, &count) && count == expected);
    CHECK(completion_file_unlock(file));
}

static void check_pid_file(const char *directory, pid_t pid)
{
    char path[256];
    CHECK(snprintf(path, sizeof(path), "%s/analyst.pid", directory) > 0);
    FILE *file = fopen(path, "r");
    CHECK(file != NULL);
    long observed = 0;
    CHECK(fscanf(file, "%ld", &observed) == 1 && observed == (long)pid);
    CHECK(fclose(file) == 0);
}

static void check_output(int pipe_fd, int first, int last)
{
    char expected[256] = {0};
    size_t used = 0;
    for (int pid = first; pid <= last; pid++) {
        int length = snprintf(expected + used, sizeof(expected) - used,
                              "%d\n", pid);
        CHECK(length > 0 && (size_t)length < sizeof(expected) - used);
        used += (size_t)length;
    }
    char actual[256];
    ssize_t count = read(pipe_fd, actual, sizeof(actual));
    if (used == 0) {
        CHECK(count == -1 && errno == EAGAIN);
    } else {
        CHECK(count == (ssize_t)used);
        CHECK(memcmp(actual, expected, used) == 0);
        CHECK(read(pipe_fd, actual, sizeof(actual)) == -1 && errno == EAGAIN);
    }
}

static void wake_and_check(struct processes_watcher *watcher,
                           pid_t analyst, int pipe_fd,
                           int first, int last)
{
    CHECK(processes_send(analyst, SIGCONT));
    struct processes_event event = wait_event(watcher, analyst,
                                               PROCESSES_STOPPED);
    CHECK(event.code == SIGSTOP);
    check_output(pipe_fd, first, last);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && argv[1][0] == '/');
    char directory[] = "/tmp/analyst_us11_XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    struct ipc_service ipc = {0};
    struct processes_watcher watcher = {0};
    struct completion_file file = {0};
    CHECK(ipc_service_init(&ipc));
    CHECK(processes_watcher_init(&watcher));
    CHECK(completion_file_open(&file, directory, ipc.block, true));
    int pipe_fds[2];
    CHECK(pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) == 0);
    pid_t analyst = spawn_analyst(&watcher, argv[1], directory, pipe_fds[1]);
    CHECK(close(pipe_fds[1]) == 0);
    CHECK(wait_event(&watcher, analyst, PROCESSES_STOPPED).code == SIGSTOP);
    check_pid_file(directory, analyst);
    check_output(pipe_fds[0], 1, 0);
    wake_and_check(&watcher, analyst, pipe_fds[0], 1, 0);
    append_range(&file, 1, 1);
    wake_and_check(&watcher, analyst, pipe_fds[0], 1, 1);
    check_count(&file, 0);
    append_range(&file, 1, 10);
    wake_and_check(&watcher, analyst, pipe_fds[0], 1, 10);
    check_count(&file, 0);
    append_range(&file, 1, 11);
    wake_and_check(&watcher, analyst, pipe_fds[0], 1, 10);
    check_count(&file, 1);
    wake_and_check(&watcher, analyst, pipe_fds[0], 11, 11);
    check_count(&file, 0);
    append_range(&file, 1, 27);
    wake_and_check(&watcher, analyst, pipe_fds[0], 1, 10);
    check_count(&file, 17);
    wake_and_check(&watcher, analyst, pipe_fds[0], 11, 20);
    check_count(&file, 7);
    wake_and_check(&watcher, analyst, pipe_fds[0], 21, 27);
    check_count(&file, 0);
    CHECK(processes_send(analyst, SIGTERM));
    CHECK(processes_send(analyst, SIGCONT));
    CHECK(wait_event(&watcher, analyst, PROCESSES_SIGNALED).code == SIGTERM);
    CHECK(close(pipe_fds[0]) == 0);

    char pid_path[256];
    CHECK(snprintf(pid_path, sizeof(pid_path), "%s/analyst.pid", directory) > 0);
    CHECK(unlink(pid_path) == 0);
    int full = open("/dev/full", O_WRONLY | O_CLOEXEC);
    CHECK(full >= 0);
    analyst = spawn_analyst(&watcher, argv[1], directory, full);
    CHECK(close(full) == 0);
    CHECK(wait_event(&watcher, analyst, PROCESSES_STOPPED).code == SIGSTOP);
    append_range(&file, 77, 77);
    CHECK(processes_send(analyst, SIGCONT));
    CHECK(wait_event(&watcher, analyst, PROCESSES_EXITED_ERROR).code != 0);
    check_count(&file, 1);
    CHECK(unlink(pid_path) == 0);

    int corrupt = openat(file.directory_fd, COMPLETION_FILE_NAME,
                         O_WRONLY | O_APPEND | O_CLOEXEC);
    CHECK(corrupt >= 0);
    CHECK(write(corrupt, "bad\n", 4) == 4);
    CHECK(close(corrupt) == 0);
    int discard = open("/dev/null", O_WRONLY | O_CLOEXEC);
    CHECK(discard >= 0);
    analyst = spawn_analyst(&watcher, argv[1], directory, discard);
    CHECK(close(discard) == 0);
    CHECK(wait_event(&watcher, analyst, PROCESSES_STOPPED).code == SIGSTOP);
    CHECK(processes_send(analyst, SIGCONT));
    CHECK(wait_event(&watcher, analyst, PROCESSES_EXITED_ERROR).code != 0);
    CHECK(unlink(pid_path) == 0);
    int preserved = openat(file.directory_fd, COMPLETION_FILE_NAME,
                           O_RDONLY | O_CLOEXEC);
    CHECK(preserved >= 0);
    char contents[16];
    CHECK(read(preserved, contents, sizeof(contents)) == 7);
    CHECK(memcmp(contents, "77\nbad\n", 7) == 0);
    CHECK(close(preserved) == 0);

    CHECK(completion_file_close(&file));
    CHECK(processes_watcher_close(&watcher));
    CHECK(ipc_service_close(&ipc));
    char completed_path[256];
    CHECK(snprintf(completed_path, sizeof(completed_path), "%s/%s", directory,
                   COMPLETION_FILE_NAME) > 0);
    CHECK(unlink(completed_path) == 0);
    CHECK(rmdir(directory) == 0);
    int status;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    puts("Analista: paradas e lotes reais passaram (US11).");
    return EXIT_SUCCESS;
}
