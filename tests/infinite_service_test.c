#define _GNU_SOURCE
#include "app/service.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int run_service(const char *analyst_path, const char *client_path)
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
    bool prepared = service_prepare(&service, &config);
    bool ran = prepared && service_run_infinite(&service, client_path, &result);
    bool closed = service_close(&service, &result);
    free(root);
    if (!prepared || !ran || !closed || !result.complete ||
        result.created != result.completed + result.cancelled ||
        result.failed != 0 || result.completed == 0 ||
        result.cancelled == 0 || result.elapsed_ns == 0) return 1;
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

int main(int argc, char **argv)
{
    assert(argc == 3);
    int input[2];
    assert(pipe(input) == 0);
    pid_t service_pid = fork();
    assert(service_pid >= 0);
    if (service_pid == 0) {
        close(input[1]);
        if (dup2(input[0], STDIN_FILENO) == -1) _exit(1);
        close(input[0]);
        _exit(run_service(argv[1], argv[2]));
    }
    close(input[0]);
    struct timespec pause = {.tv_sec = 1, .tv_nsec = 0};
    while (nanosleep(&pause, &pause) == -1 && errno == EINTR) {}
    assert(write(input[1], "s", 1) == 1);
    assert(close(input[1]) == 0);
    int status;
    assert(waitpid(service_pid, &status, 0) == service_pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    return 0;
}
