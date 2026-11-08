#define _GNU_SOURCE
#include "app/analyst_coordinator.h"
#include "platform/ipc.h"

#include <errno.h>
#include <dirent.h>
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

#define TEST_PROGRESS_EVENT 2u

struct test_context {
    struct processes_watcher watcher;
    struct scheduler scheduler;
    struct completion_file file;
    struct analyst_coordinator coordinator;
    size_t batches[8];
    size_t batch_count;
    bool deferred_checked;
    bool reception_progressed;
    bool worker_finished;
    int event_error;
};

static void on_process_event(void *argument, const struct processes_event *event)
{
    struct test_context *context = argument;
    if (event->pid != context->coordinator.pid) {
        context->event_error = EPROTO;
        return;
    }
    if (event->kind == PROCESSES_STOPPED &&
        context->coordinator.state == ANALYST_COORDINATOR_ACTIVE) {
        CHECK(context->batch_count < 8);
        context->batches[context->batch_count++] =
            context->coordinator.expected_printed;
    }
    if (!analyst_coordinator_observe(&context->coordinator, event)) {
        context->event_error = errno;
    }
}

static bool process_events(void *argument)
{
    struct test_context *context = argument;
    if (!processes_collect(&context->watcher, on_process_event, context, NULL)) {
        return false;
    }
    if (context->event_error != 0) {
        errno = context->event_error;
        return false;
    }
    bool woken;
    if (!analyst_coordinator_maybe_wake(&context->coordinator, false, &woken)) {
        return false;
    }
    if (woken && !context->deferred_checked) {
        bool appended = true;
        CHECK(analyst_coordinator_try_append(&context->coordinator, 999,
                                              &appended) && !appended);
        CHECK(analyst_coordinator_maybe_wake(&context->coordinator, true,
                                              &woken) && !woken);
        context->deferred_checked = true;
        CHECK(scheduler_notify(&context->scheduler, TEST_PROGRESS_EVENT) >= 0);
    }
    return true;
}

static bool wait_events(void *argument)
{
    struct test_context *context = argument;
    return processes_wait(&context->watcher);
}

static bool progress_ready(void *argument)
{
    const struct test_context *context = argument;
    return context->deferred_checked;
}

static bool analyst_stopped(void *argument)
{
    const struct analyst_coordinator *coordinator = argument;
    return coordinator->state == ANALYST_COORDINATOR_STOPPED ||
           coordinator->state == ANALYST_COORDINATOR_DEAD;
}

static void reception_worker(void *argument)
{
    struct test_context *context = argument;
    CHECK(scheduler_wait(&context->scheduler, TEST_PROGRESS_EVENT,
                         progress_ready, context));
    CHECK(context->coordinator.state == ANALYST_COORDINATOR_ACTIVE);
    context->reception_progressed = true;
}

static void attendant_worker(void *argument)
{
    struct test_context *context = argument;
    CHECK(scheduler_wait(&context->scheduler, TEST_PROGRESS_EVENT,
                         progress_ready, context));
    CHECK(context->coordinator.state == ANALYST_COORDINATOR_ACTIVE);
    CHECK(analyst_coordinator_request_drain(&context->coordinator));
    bool appended = false;
    while (!appended) {
        CHECK(analyst_coordinator_try_append(&context->coordinator, 28,
                                              &appended));
        if (!appended) {
            CHECK(scheduler_wait(&context->scheduler,
                                 ANALYST_COORDINATOR_EVENT,
                                 analyst_coordinator_can_append,
                                 &context->coordinator));
        }
    }
    for (;;) {
        bool drained = false;
        CHECK(analyst_coordinator_is_drained(&context->coordinator, &drained));
        if (drained) break;
        if (context->coordinator.state == ANALYST_COORDINATOR_ACTIVE ||
            context->coordinator.state == ANALYST_COORDINATOR_STARTING) {
            CHECK(scheduler_wait(&context->scheduler,
                                 ANALYST_COORDINATOR_EVENT, analyst_stopped,
                                 &context->coordinator));
        } else {
            CHECK(scheduler_yield());
        }
    }
    context->worker_finished = true;
}

static pid_t spawn_analyst(struct processes_watcher *watcher,
                           const char *path, const char *directory,
                           int output_fd)
{
    int saved = dup(STDOUT_FILENO);
    CHECK(saved >= 0);
    CHECK(dup2(output_fd, STDOUT_FILENO) == STDOUT_FILENO);
    char *arguments[] = {(char *)path, (char *)directory, NULL};
    pid_t pid = -1;
    bool spawned = processes_spawn(watcher, path, arguments, directory, &pid);
    int error = errno;
    CHECK(dup2(saved, STDOUT_FILENO) == STDOUT_FILENO);
    CHECK(close(saved) == 0);
    errno = error;
    CHECK(spawned && pid > 0);
    return pid;
}

static void check_output(int descriptor)
{
    char output[256];
    size_t used = 0;
    for (;;) {
        ssize_t count = read(descriptor, output + used, sizeof(output) - used);
        if (count == -1 && errno == EINTR) continue;
        CHECK(count >= 0);
        if (count == 0) break;
        used += (size_t)count;
        CHECK(used < sizeof(output));
    }
    char expected[256];
    size_t expected_size = 0;
    for (int pid = 1; pid <= 28; pid++) {
        int count = snprintf(expected + expected_size,
                             sizeof(expected) - expected_size, "%d\n", pid);
        CHECK(count > 0 && (size_t)count < sizeof(expected) - expected_size);
        expected_size += (size_t)count;
    }
    CHECK(used == expected_size && memcmp(output, expected, used) == 0);
}

static struct processes_event collect_terminal(struct processes_watcher *watcher,
                                                pid_t pid)
{
    struct processes_event result = {0};
    for (;;) {
        int raw;
        pid_t observed = waitpid(pid, &raw, WNOHANG | WUNTRACED | WCONTINUED);
        if (observed == pid && (WIFEXITED(raw) || WIFSIGNALED(raw))) {
            result.pid = pid;
            result.raw_status = raw;
            result.kind = WIFSIGNALED(raw) ? PROCESSES_SIGNALED :
                          WEXITSTATUS(raw) == 0 ? PROCESSES_EXITED_OK :
                          PROCESSES_EXITED_ERROR;
            result.code = WIFSIGNALED(raw) ? WTERMSIG(raw) : WEXITSTATUS(raw);
            return result;
        }
        CHECK(observed >= 0);
        CHECK(processes_wait(watcher));
    }
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && argv[1][0] == '/');
    char directory[] = "/tmp/analyst_us12_XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    struct ipc_service ipc = {0};
    struct test_context context = {0};
    CHECK(ipc_service_init(&ipc));
    CHECK(processes_watcher_init(&context.watcher));
    CHECK(completion_file_open(&context.file, directory, ipc.block, true));
    bool acquired;
    CHECK(completion_file_try_lock(&context.file, &acquired) && acquired);
    for (int pid = 1; pid <= 27; pid++) {
        CHECK(completion_file_append(&context.file, (pid_t)pid));
    }
    CHECK(completion_file_unlock(&context.file));
    int pipe_fds[2];
    CHECK(pipe2(pipe_fds, O_CLOEXEC) == 0);
    pid_t analyst = spawn_analyst(&context.watcher, argv[1], directory,
                                  pipe_fds[1]);
    CHECK(close(pipe_fds[1]) == 0);
    CHECK(scheduler_init(&context.scheduler, NULL, NULL, NULL));
    CHECK(analyst_coordinator_init(&context.coordinator, analyst,
                                    &context.file, &context.scheduler));
    CHECK(scheduler_create(&context.scheduler, 0, reception_worker, &context));
    CHECK(scheduler_create(&context.scheduler, 1, attendant_worker, &context));
    CHECK(scheduler_run_events(&context.scheduler, process_events,
                                wait_events, &context));
    CHECK(context.deferred_checked && context.reception_progressed &&
          context.worker_finished);
    CHECK(context.coordinator.state == ANALYST_COORDINATOR_STOPPED);
    CHECK(context.batch_count == 4);
    CHECK(context.batches[0] == 10 && context.batches[1] == 10 &&
          context.batches[2] == 7 && context.batches[3] == 1);
    CHECK(scheduler_destroy(&context.scheduler));
    CHECK(processes_send(analyst, SIGTERM));
    CHECK(processes_send(analyst, SIGCONT));
    CHECK(collect_terminal(&context.watcher, analyst).code == SIGTERM);
    check_output(pipe_fds[0]);
    CHECK(close(pipe_fds[0]) == 0);

    char pid_path[256];
    CHECK(snprintf(pid_path, sizeof(pid_path), "%s/analyst.pid", directory) > 0);
    CHECK(unlink(pid_path) == 0);
    int discard = open("/dev/null", O_WRONLY | O_CLOEXEC);
    CHECK(discard >= 0);
    analyst = spawn_analyst(&context.watcher, argv[1], directory, discard);
    CHECK(close(discard) == 0);
    CHECK(scheduler_init(&context.scheduler, NULL, NULL, NULL));
    struct analyst_coordinator dying = {0};
    CHECK(analyst_coordinator_init(&dying, analyst, &context.file,
                                    &context.scheduler));
    struct processes_event stop = {0};
    for (;;) {
        int raw;
        pid_t observed = waitpid(analyst, &raw, WNOHANG | WUNTRACED);
        CHECK(observed >= 0);
        if (observed == analyst) {
            CHECK(WIFSTOPPED(raw));
            stop.pid = analyst;
            stop.kind = PROCESSES_STOPPED;
            stop.code = WSTOPSIG(raw);
            CHECK(analyst_coordinator_observe(&dying, &stop));
            break;
        }
        CHECK(processes_wait(&context.watcher));
    }
    bool appended = false;
    bool woken = true;
    CHECK(analyst_coordinator_try_append(&dying, 50, &appended) && appended);
    CHECK(analyst_coordinator_maybe_wake(&dying, false, &woken) && !woken);
    CHECK(analyst_coordinator_maybe_wake(&dying, true, &woken) && woken);
    CHECK(analyst_coordinator_maybe_wake(&dying, true, &woken) && !woken);
    for (;;) {
        int raw;
        pid_t observed = waitpid(analyst, &raw, WNOHANG | WUNTRACED);
        CHECK(observed >= 0);
        if (observed == analyst) {
            CHECK(WIFSTOPPED(raw));
            stop.code = WSTOPSIG(raw);
            CHECK(analyst_coordinator_observe(&dying, &stop));
            CHECK(dying.pending == 0);
            break;
        }
        CHECK(processes_wait(&context.watcher));
    }
    CHECK(analyst_coordinator_try_append(&dying, 51, &appended) && appended);
    CHECK(analyst_coordinator_maybe_wake(&dying, true, &woken) && woken);
    CHECK(processes_send(analyst, SIGKILL));
    struct processes_event killed = collect_terminal(&context.watcher, analyst);
    CHECK(killed.kind == PROCESSES_SIGNALED && killed.code == SIGKILL);
    CHECK(!analyst_coordinator_observe(&dying, &killed) && errno == ECHILD);
    CHECK(dying.state == ANALYST_COORDINATOR_DEAD);
    CHECK(scheduler_destroy(&context.scheduler));
    CHECK(unlink(pid_path) == 0);
    CHECK(completion_file_close(&context.file));
    CHECK(processes_watcher_close(&context.watcher));
    CHECK(ipc_service_close(&ipc));
    char completed_path[256];
    CHECK(snprintf(completed_path, sizeof(completed_path), "%s/%s", directory,
                   COMPLETION_FILE_NAME) > 0);
    CHECK(unlink(completed_path) == 0);
    DIR *entries = opendir(directory);
    CHECK(entries != NULL);
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        if (strncmp(entry->d_name, ".completed.", 11) == 0 &&
            strstr(entry->d_name, ".tmp") != NULL) {
            CHECK(unlinkat(dirfd(entries), entry->d_name, 0) == 0);
        }
    }
    CHECK(closedir(entries) == 0);
    CHECK(rmdir(directory) == 0);
    int status;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    puts("Coordenação: drenagem, espera e morte do Analista passaram (US12).");
    return EXIT_SUCCESS;
}
