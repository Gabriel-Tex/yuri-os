#define _GNU_SOURCE
#include "platform/processes.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition)                                                            \
    do                                                                              \
    {                                                                               \
        if (!(condition))                                                           \
        {                                                                           \
            fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                                     \
        }                                                                           \
    } while (0)

#define EVENT_CAPACITY 32

struct event_log
{
    struct processes_event events[EVENT_CAPACITY];
    size_t count;
};

static void record_event(void *context, const struct processes_event *event)
{
    struct event_log *log = context;
    CHECK(log->count < EVENT_CAPACITY);
    log->events[log->count++] = *event;
}

static const struct processes_event *find_event(const struct event_log *log,
                                                pid_t pid,
                                                enum processes_event_kind kind)
{
    for (size_t i = 0; i < log->count; i++)
    {
        if (log->events[i].pid == pid && log->events[i].kind == kind)
        {
            return &log->events[i];
        }
    }
    return NULL;
}

static const struct processes_event *wait_for_event(struct processes_watcher *watcher,
                                                    struct event_log *log,
                                                    pid_t pid,
                                                    enum processes_event_kind kind)
{
    const struct processes_event *found;
    while ((found = find_event(log, pid, kind)) == NULL)
    {
        CHECK(processes_collect(watcher, record_event, log, NULL));
        found = find_event(log, pid, kind);
        if (found == NULL)
        {
            CHECK(processes_wait(watcher));
        }
    }
    return found;
}

static pid_t spawn_mode(struct processes_watcher *watcher, const char *path,
                        const char *mode, const char *extra,
                        const char *working_directory)
{
    char *arguments[] = {(char *)path, (char *)mode, (char *)extra, NULL};
    pid_t pid = -1;
    CHECK(processes_spawn(watcher, path, arguments, working_directory, &pid));
    CHECK(pid > 0);
    return pid;
}

static void check_reaped(pid_t pid)
{
    int status;
    CHECK(waitpid(pid, &status, WNOHANG) == -1);
    CHECK(errno == ECHILD);
}

static void test_basic_outcomes(struct processes_watcher *watcher,
                                const char *path)
{
    struct event_log log = {0};
    pid_t success = spawn_mode(watcher, path, "exit0", NULL, NULL);
    const struct processes_event *event = wait_for_event(watcher, &log, success,
                                                         PROCESSES_EXITED_OK);
    CHECK(event->code == 0 && WIFEXITED(event->raw_status));
    check_reaped(success);

    log.count = 0;
    pid_t failure = spawn_mode(watcher, path, "exit7", NULL, NULL);
    event = wait_for_event(watcher, &log, failure, PROCESSES_EXITED_ERROR);
    CHECK(event->code == 7 && WEXITSTATUS(event->raw_status) == 7);
    check_reaped(failure);

    log.count = 0;
    pid_t signaled = spawn_mode(watcher, path, "signal", NULL, NULL);
    event = wait_for_event(watcher, &log, signaled, PROCESSES_SIGNALED);
    CHECK(event->code == SIGTERM && WIFSIGNALED(event->raw_status));
    check_reaped(signaled);
}

static void test_stop_and_continue(struct processes_watcher *watcher,
                                   const char *path)
{
    struct event_log log = {0};
    pid_t pid = spawn_mode(watcher, path, "stop", NULL, NULL);
    const struct processes_event *event = wait_for_event(watcher, &log, pid,
                                                         PROCESSES_STOPPED);
    CHECK(event->code == SIGSTOP && WIFSTOPPED(event->raw_status));
    CHECK(processes_send(pid, SIGCONT));
    event = wait_for_event(watcher, &log, pid, PROCESSES_CONTINUED);
    CHECK(event->code == SIGCONT);
    CHECK(processes_send(pid, SIGUSR1));
    event = wait_for_event(watcher, &log, pid, PROCESSES_EXITED_OK);
    CHECK(event->code == 0);
    check_reaped(pid);
}

static void test_exec_and_setup_errors(struct processes_watcher *watcher,
                                       const char *path)
{
    struct event_log log = {0};
    char *arguments[] = {"missing-client", NULL};
    pid_t pid = -1;
    CHECK(processes_spawn(watcher, "/no/such/official-client", arguments,
                          NULL, &pid));
    const struct processes_event *event = wait_for_event(watcher, &log, pid,
                                                         PROCESSES_EXITED_ERROR);
    CHECK(event->code == 127);
    check_reaped(pid);

    log.count = 0;
    pid = spawn_mode(watcher, path, "exit0", NULL, "/no/such/work-directory");
    event = wait_for_event(watcher, &log, pid, PROCESSES_EXITED_ERROR);
    CHECK(event->code == 127);
    check_reaped(pid);
}

static void test_child_environment(struct processes_watcher *watcher,
                                   const char *path)
{
    struct event_log log = {0};
    pid_t pid = spawn_mode(watcher, path, "mask", NULL, NULL);
    CHECK(wait_for_event(watcher, &log, pid, PROCESSES_EXITED_OK)->code == 0);
    check_reaped(pid);

    char directory[] = "/tmp/processes_us07_XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    log.count = 0;
    pid = spawn_mode(watcher, path, "cwd", directory, directory);
    CHECK(wait_for_event(watcher, &log, pid, PROCESSES_EXITED_OK)->code == 0);
    check_reaped(pid);
    CHECK(rmdir(directory) == 0);

    int descriptors[2];
    CHECK(pipe(descriptors) == 0);
    char number[32];
    CHECK(snprintf(number, sizeof(number), "%d", descriptors[1]) > 0);
    log.count = 0;
    pid = spawn_mode(watcher, path, "fd", number, NULL);
    CHECK(close(descriptors[0]) == 0);
    CHECK(close(descriptors[1]) == 0);
    CHECK(wait_for_event(watcher, &log, pid, PROCESSES_EXITED_OK)->code == 0);
    check_reaped(pid);
}

static void test_grouped_exits(struct processes_watcher *watcher,
                               const char *path)
{
    struct event_log log = {0};
    pid_t children[4];
    for (size_t i = 0; i < 4; i++)
    {
        children[i] = spawn_mode(watcher, path, "exit0", NULL, NULL);
    }
    /* WNOWAIT confirma cada término sem consumir os eventos que o adaptador
     * deve recolher na chamada única abaixo. */
    for (size_t i = 0; i < 4; i++)
    {
        siginfo_t information;
        int result;
        do
        {
            result = waitid(P_PID, (id_t)children[i], &information,
                            WEXITED | WNOWAIT);
        } while (result == -1 && errno == EINTR);
        CHECK(result == 0 && information.si_pid == children[i]);
    }
    size_t first_batch = 0;
    CHECK(processes_collect(watcher, record_event, &log, &first_batch));
    CHECK(first_batch == 4 && log.count == 4);
    for (size_t i = 0; i < 4; i++)
    {
        const struct processes_event *event = wait_for_event(watcher, &log,
                                                             children[i],
                                                             PROCESSES_EXITED_OK);
        CHECK(event->code == 0);
        check_reaped(children[i]);
    }
    CHECK(log.count == 4);
    size_t empty_batch = 99;
    CHECK(processes_collect(watcher, record_event, &log, &empty_batch));
    CHECK(empty_batch == 0);
}

static void test_invalid_calls(struct processes_watcher *watcher,
                               const char *path)
{
    pid_t output = -5;
    char *arguments[] = {(char *)path, "exit0", NULL};
    CHECK(!processes_spawn(watcher, "relative-client", arguments, NULL,
                           &output));
    CHECK(errno == EINVAL && output == -5);
    CHECK(!processes_send(0, SIGTERM));
    CHECK(errno == EINVAL);
    CHECK(!processes_collect(watcher, NULL, NULL, NULL));
    CHECK(errno == EINVAL);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && argv[1][0] == '/');
    sigset_t original_mask;
    CHECK(sigprocmask(SIG_BLOCK, NULL, &original_mask) == 0);
    sigset_t child_mask = original_mask;
    CHECK(sigdelset(&child_mask, SIGCHLD) == 0);
    CHECK(sigaddset(&child_mask, SIGUSR2) == 0);
    CHECK(sigprocmask(SIG_SETMASK, &child_mask, NULL) == 0);
    struct sigaction original_action;
    CHECK(sigaction(SIGCHLD, NULL, &original_action) == 0);

    struct processes_watcher watcher = {0};
    CHECK(processes_watcher_init(&watcher));
    CHECK(!processes_watcher_init(&watcher));
    CHECK(errno == EINVAL);
    test_invalid_calls(&watcher, argv[1]);
    test_basic_outcomes(&watcher, argv[1]);
    test_stop_and_continue(&watcher, argv[1]);
    test_exec_and_setup_errors(&watcher, argv[1]);
    test_child_environment(&watcher, argv[1]);
    test_grouped_exits(&watcher, argv[1]);
    CHECK(processes_watcher_close(&watcher));
    CHECK(processes_watcher_close(&watcher));
    struct sigaction restored_action;
    sigset_t restored_mask;
    CHECK(sigaction(SIGCHLD, NULL, &restored_action) == 0);
    CHECK(sigprocmask(SIG_BLOCK, NULL, &restored_mask) == 0);
    CHECK(restored_action.sa_handler == original_action.sa_handler);
    CHECK(sigismember(&restored_mask, SIGCHLD) == 0);
    CHECK(sigismember(&restored_mask, SIGUSR2) == 1);

    CHECK(sigaddset(&child_mask, SIGCHLD) == 0);
    CHECK(sigprocmask(SIG_SETMASK, &child_mask, NULL) == 0);
    struct processes_watcher blocked_watcher = {0};
    struct event_log blocked_log = {0};
    CHECK(processes_watcher_init(&blocked_watcher));
    pid_t blocked_child = spawn_mode(&blocked_watcher, argv[1], "exit0",
                                     NULL, NULL);
    CHECK(processes_wait(&blocked_watcher));
    CHECK(wait_for_event(&blocked_watcher, &blocked_log, blocked_child,
                         PROCESSES_EXITED_OK)
              ->code == 0);
    check_reaped(blocked_child);
    CHECK(processes_watcher_close(&blocked_watcher));
    CHECK(sigprocmask(SIG_BLOCK, NULL, &restored_mask) == 0);
    CHECK(sigismember(&restored_mask, SIGCHLD) == 1);
    CHECK(sigprocmask(SIG_SETMASK, &original_mask, NULL) == 0);
    int status;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    puts("Processos: criação, sinais e coleta passaram (US07).");
    return EXIT_SUCCESS;
}
