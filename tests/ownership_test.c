#define _GNU_SOURCE
#include "app/workers.h"

#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: falhou: %s (errno %d)\n", \
            __FILE__, __LINE__, #condition, errno); exit(EXIT_FAILURE); \
} } while (0)

struct start_state {
    pid_t pid;
    bool stopped;
    bool failed;
};

static void observe_start(void *context, const struct processes_event *event)
{
    struct start_state *state = context;
    if (event->pid != state->pid ||
        (event->kind != PROCESSES_STOPPED &&
         event->kind != PROCESSES_CONTINUED)) {
        state->failed = true;
    } else if (event->kind == PROCESSES_STOPPED && event->code == SIGSTOP) {
        state->stopped = true;
    }
}

static pid_t spawn_stopped(struct service_state *service,
                            const char *client_path)
{
    char *arguments[] = {(char *)client_path, "demand", "1", NULL};
    pid_t pid;
    CHECK(processes_spawn(&service->watcher, client_path, arguments,
                          service->run_directory, &pid));
    struct start_state state = {.pid = pid};
    while (!state.stopped && !state.failed) {
        CHECK(processes_collect(&service->watcher, observe_start, &state,
                                NULL));
        if (!state.stopped && !state.failed)
            CHECK(processes_wait(&service->watcher));
    }
    CHECK(state.stopped && !state.failed);
    CHECK(unlinkat(service->completion.directory_fd, DEMAND_FILE_NAME, 0) == 0);
    return pid;
}

static void wait_analyst(struct service_state *service)
{
    CHECK(ipc_service_create_semaphores(&service->ipc));
    CHECK(service_start_analyst(service));
    while (!service_analyst_ready(service)) {
        CHECK(service_process_events(service));
        if (!service_analyst_ready(service)) CHECK(service_wait_events(service));
    }
}

static void check_released(void)
{
    errno = 0;
    CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    errno = 0;
    CHECK(sem_open(IPC_ATTEND_NAME, 0) == SEM_FAILED && errno == ENOENT);
    errno = 0;
    CHECK(sem_open(IPC_BLOCK_NAME, 0) == SEM_FAILED && errno == ENOENT);
}

static void run_ownership_case(const char *analyst_path,
                                const char *client_path, bool grouped)
{
    char *root = getcwd(NULL, 0);
    CHECK(root != NULL);
    struct service_config config = {.client_count = 3,
        .max_patience_ms = 1000, .project_root = root,
        .analyst_path = analyst_path};
    struct service_state service = {0};
    struct client_queue queue;
    struct metrics metrics;
    struct workers_reception reception = {0};
    struct workers_attendant attendant = {0};
    bool stop = false;
    char *arguments[] = {(char *)client_path, NULL};
    CHECK(service_prepare(&service, &config));
    wait_analyst(&service);
    CHECK(client_queue_init(&queue) && metrics_init(&metrics));
    CHECK(workers_reception_init(&reception, &service, &queue, &metrics,
                                  client_path, arguments, &stop));
    CHECK(workers_attendant_init(&attendant, &service, &queue, &metrics,
                                  &reception.finished));
    reception.other_handler = workers_attendant_on_event;
    reception.other_context = &attendant;
    reception.attendant = &attendant;
    attendant.reception = &reception;

    pid_t queued_pid = spawn_stopped(&service, client_path);
    struct client_record queued;
    CHECK(metrics_record_created(&metrics));
    CHECK(client_init(&queued, queued_pid, 0, CLIENT_PRIORITY_LOW, 0, 1000));
    CHECK(client_enqueue(&queued, 1));
    CHECK(client_queue_reserve(&queue) && client_queue_insert(&queue, &queued));

    pid_t active_pid = spawn_stopped(&service, client_path);
    CHECK(metrics_record_created(&metrics));
    CHECK(client_init(&attendant.active, active_pid, 1,
                      CLIENT_PRIORITY_LOW, 0, 1000));
    CHECK(client_enqueue(&attendant.active, 1));
    CHECK(client_start(&attendant.active));
    attendant.active_valid = true;

    pid_t starting_pid = spawn_stopped(&service, client_path);
    CHECK(metrics_record_created(&metrics));
    CHECK(client_init(&reception.starting, starting_pid, 2,
                      CLIENT_PRIORITY_LOW, 0, 1000));
    CHECK(client_queue_reserve(&queue));
    reception.starting_active = true;
    reception.starting_record_valid = true;

    CHECK(processes_send(queued_pid, SIGKILL));
    CHECK(processes_send(starting_pid, SIGKILL));
    if (grouped) CHECK(processes_send(active_pid, SIGKILL));
    struct timespec pause = {.tv_nsec = 20000000};
    while (nanosleep(&pause, &pause) == -1 && errno == EINTR) {}

    if (grouped) {
        CHECK(!service_process_events(&service));
        CHECK(reception.starting_reaped && reception.reaped_queued_count == 1 &&
              attendant.active_reaped);
    }
    CHECK(workers_attendant_close(&attendant));
    CHECK(reception.starting_reaped && reception.reaped_queued_count == 1);
    CHECK(workers_reception_close(&reception));
    CHECK(metrics.created == 3 && metrics.completed == 0 &&
          metrics.failed == 3 && metrics.cancelled == 0);
    CHECK(service_close(&service, NULL));
    check_released();
    free(root);
}

static void run_blocked_term_case(const char *analyst_path)
{
    sigset_t before;
    sigset_t blocked;
    CHECK(sigemptyset(&blocked) == 0);
    CHECK(sigaddset(&blocked, SIGTERM) == 0);
    CHECK(sigprocmask(SIG_BLOCK, &blocked, &before) == 0);
    char *root = getcwd(NULL, 0);
    CHECK(root != NULL);
    struct service_config config = {.client_count = 1,
        .max_patience_ms = 1000, .project_root = root,
        .analyst_path = analyst_path};
    struct service_state service = {0};
    CHECK(service_prepare(&service, &config));
    wait_analyst(&service);
    CHECK(service_close(&service, NULL));
    sigset_t after;
    CHECK(sigprocmask(SIG_BLOCK, NULL, &after) == 0);
    CHECK(sigismember(&after, SIGTERM) == 1);
    CHECK(sigprocmask(SIG_SETMASK, &before, NULL) == 0);
    check_released();
    free(root);
}

int main(int argc, char **argv)
{
    CHECK(argc == 3);
    run_ownership_case(argv[1], argv[2], true);
    run_ownership_case(argv[1], argv[2], false);
    run_blocked_term_case(argv[1]);
    puts("Propriedade dos filhos: eventos agrupados, limpeza cruzada e SIGTERM bloqueado passaram.");
    return EXIT_SUCCESS;
}
