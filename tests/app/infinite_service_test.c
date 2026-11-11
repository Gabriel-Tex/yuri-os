#define _GNU_SOURCE
#include "app/service.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int run_service(const char *analyst_path, const char *client_path,
                       int ready_descriptor, bool expect_success)
{
    int sink = open("/dev/null", O_WRONLY);
    if (sink == -1 || dup2(sink, STDOUT_FILENO) == -1) return 1;
    close(sink);
    char *root = getcwd(NULL, 0);
    if (root == NULL) return 1;
    struct service_config config = {.client_count = 0,
        .max_patience_ms = 10000, .project_root = root,
        .analyst_path = analyst_path};
    struct service_state service = {0};
    struct service_result result = {0};
    sigset_t original_mask;
    struct sigaction original_int;
    struct sigaction original_term;
    if (sigprocmask(SIG_BLOCK, NULL, &original_mask) == -1 ||
        sigaction(SIGINT, NULL, &original_int) == -1 ||
        sigaction(SIGTERM, NULL, &original_term) == -1) return 1;
    bool prepared = service_prepare(&service, &config);
    if (prepared && write(ready_descriptor, "r", 1) != 1) return 1;
    close(ready_descriptor);
    bool ran = prepared && service_run_infinite(&service, client_path, &result);
    int run_error = errno;
    bool closed = service_close(&service, &result);
    int close_error = errno;
    free(root);
    if (!prepared || ran != expect_success || !closed ||
        result.complete != expect_success ||
        result.created != result.completed + result.cancelled + result.failed ||
        result.elapsed_ns == 0) {
        fprintf(stderr, "run_service: prep=%d ran=%d close=%d complete=%d created=%llu completed=%llu cancelled=%llu failed=%llu errors=%d,%d\n",
                prepared, ran, closed, result.complete,
                (unsigned long long)result.created,
                (unsigned long long)result.completed,
                (unsigned long long)result.cancelled,
                (unsigned long long)result.failed, run_error, close_error);
        return 1;
    }
    if (expect_success && (result.completed == 0 || result.cancelled == 0 ||
                           result.failed != 0)) return 1;
    sigset_t restored_mask;
    struct sigaction restored_int;
    struct sigaction restored_term;
    if (sigprocmask(SIG_BLOCK, NULL, &restored_mask) == -1 ||
        sigaction(SIGINT, NULL, &restored_int) == -1 ||
        sigaction(SIGTERM, NULL, &restored_term) == -1 ||
        restored_int.sa_handler != original_int.sa_handler ||
        restored_term.sa_handler != original_term.sa_handler ||
        sigismember(&restored_mask, SIGCHLD) !=
            sigismember(&original_mask, SIGCHLD) ||
        sigismember(&restored_mask, SIGINT) !=
            sigismember(&original_mask, SIGINT) ||
        sigismember(&restored_mask, SIGTERM) !=
            sigismember(&original_mask, SIGTERM)) return 1;
    errno = 0;
    if (waitpid(-1, NULL, WNOHANG) != -1 || errno != ECHILD) return 1;
    errno = 0;
    if (sem_open(IPC_ATTEND_NAME, 0) != SEM_FAILED || errno != ENOENT)
        return 1;
    errno = 0;
    if (sem_open(IPC_BLOCK_NAME, 0) != SEM_FAILED || errno != ENOENT)
        return 1;
    return 0;
}

static void run_case(const char *analyst_path, const char *client_path,
                     int stop_signal)
{
    int input[2];
    int ready[2];
    assert(pipe(input) == 0 && pipe(ready) == 0);
    pid_t service_pid = fork();
    assert(service_pid >= 0);
    if (service_pid == 0) {
        close(input[1]);
        close(ready[0]);
        if (dup2(input[0], STDIN_FILENO) == -1) _exit(1);
        close(input[0]);
        _exit(run_service(analyst_path, client_path, ready[1],
                          stop_signal == 0));
    }
    close(input[0]);
    close(ready[1]);
    char marker;
    assert(read(ready[0], &marker, 1) == 1 && marker == 'r');
    assert(close(ready[0]) == 0);
    struct timespec pause = {.tv_sec = stop_signal == 0 ? 1 : 0,
                             .tv_nsec = stop_signal == 0 ? 0 : 300000000};
    while (nanosleep(&pause, &pause) == -1 && errno == EINTR) {}
    if (stop_signal == 0) assert(write(input[1], "s", 1) == 1);
    else assert(kill(service_pid, stop_signal) == 0);
    int status;
    assert(waitpid(service_pid, &status, 0) == service_pid);
    assert(close(input[1]) == 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        dprintf(STDERR_FILENO, "falha caso %d: status %d\n", stop_signal,
                status);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    run_case(argv[1], argv[2], 0);
    run_case(argv[1], argv[2], SIGINT);
    run_case(argv[1], argv[2], SIGTERM);
    return 0;
}
