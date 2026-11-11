#define _GNU_SOURCE
#include "app/workers.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: falhou: %s (errno %d)\n", \
            __FILE__, __LINE__, #condition, errno); exit(EXIT_FAILURE); \
} } while (0)

struct harness {
    struct service_state service;
    struct client_queue queue;
    struct metrics metrics;
    struct workers_reception reception;
    bool stop;
    bool hold_until_full;
    uint64_t stop_after;
    pid_t active_pid;
    bool active_reaped;
    uint64_t removed;
    size_t full_seen;
};

static bool ready_to_drain(void *argument)
{
    const struct harness *harness = argument;
    return client_queue_waiting_count(&harness->queue) ==
               CLIENT_QUEUE_CAPACITY || harness->reception.finished;
}

static bool client_available(void *argument)
{
    const struct harness *harness = argument;
    return harness->queue.count > 0 || harness->reception.finished;
}

static bool active_reaped(void *argument)
{
    const struct harness *harness = argument;
    return harness->active_reaped;
}

static void other_event(void *argument, const struct processes_event *event)
{
    struct harness *harness = argument;
    CHECK(event->pid == harness->active_pid);
    if (event->kind == PROCESSES_SIGNALED ||
        event->kind == PROCESSES_EXITED_OK ||
        event->kind == PROCESSES_EXITED_ERROR) {
        harness->active_reaped = true;
        CHECK(scheduler_notify(&harness->service.scheduler,
                                WORKERS_TERMINAL_EVENT) >= 0);
    }
}

static void controlled_attendant(void *argument)
{
    struct harness *harness = argument;
    if (harness->hold_until_full) {
        CHECK(scheduler_wait(&harness->service.scheduler,
                             WORKERS_CLIENT_EVENT, ready_to_drain, harness));
        if (client_queue_waiting_count(&harness->queue) ==
            CLIENT_QUEUE_CAPACITY) harness->full_seen++;
    }
    for (;;) {
        if (harness->queue.count == 0) {
            if (harness->reception.finished) break;
            CHECK(scheduler_wait(&harness->service.scheduler,
                                 WORKERS_CLIENT_EVENT, client_available,
                                 harness));
            continue;
        }
        struct client_record client;
        CHECK(client_queue_take_next(&harness->queue, &client));
        CHECK(client.state == CLIENT_QUEUED && client.demand_us == 7);
        CHECK(client.patience_ns ==
              (client.priority == CLIENT_PRIORITY_HIGH ?
               UINT64_C(2500000) : UINT64_C(5000000)));
        CHECK(client.deadline_ns == client.arrival_ns + client.patience_ns);
        CHECK(scheduler_notify(&harness->service.scheduler,
                                WORKERS_SPACE_EVENT) >= 0);
        harness->active_pid = client.pid;
        harness->active_reaped = false;
        CHECK(processes_send(client.pid, SIGKILL));
        CHECK(scheduler_wait(&harness->service.scheduler,
                             WORKERS_TERMINAL_EVENT, active_reaped, harness));
        CHECK(metrics_record_cancelled(&harness->metrics, &client));
        harness->active_pid = 0;
        harness->removed++;
        if (harness->stop_after > 0 &&
            harness->metrics.created >= harness->stop_after) {
            harness->stop = true;
            CHECK(scheduler_notify(&harness->service.scheduler,
                                    WORKERS_SPACE_EVENT) >= 0);
        }
    }
}

static void run_case(const char *analyst_path, const char *client_path,
                     uint64_t count, uint64_t stop_after, bool hold_until_full)
{
    char root[] = "/tmp/reception us14 XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    struct harness harness = {.hold_until_full = hold_until_full,
                              .stop_after = stop_after};
    struct service_config config = {.client_count = count,
                                    .max_patience_ms = 5,
                                    .project_root = root,
                                    .analyst_path = analyst_path};
    CHECK(service_prepare(&harness.service, &config));
    CHECK(client_queue_init(&harness.queue));
    CHECK(metrics_init(&harness.metrics));
    char *client_argv[] = {(char *)client_path, "demand", "7", NULL};
    CHECK(workers_reception_init(&harness.reception, &harness.service,
                                  &harness.queue, &harness.metrics,
                                  client_path, client_argv, &harness.stop));
    harness.reception.other_handler = other_event;
    harness.reception.other_context = &harness;
    CHECK(scheduler_create(&harness.service.scheduler, 0,
                            workers_reception_run, &harness.reception));
    CHECK(scheduler_create(&harness.service.scheduler, 1,
                            controlled_attendant, &harness));
    CHECK(scheduler_run_events(&harness.service.scheduler,
                                service_process_events, service_wait_events,
                                &harness.service));
    CHECK(harness.reception.finished && harness.reception.error == 0);
    CHECK(harness.metrics.created == harness.removed);
    CHECK(harness.metrics.cancelled == harness.removed);
    CHECK(harness.queue.count == 0 && !harness.queue.reserved);
    CHECK(harness.reception.maximum_waiting <= CLIENT_QUEUE_CAPACITY);
    if (count > 0) {
        CHECK(harness.metrics.created == count);
    } else {
        CHECK(harness.stop && harness.metrics.created >= stop_after);
    }
    if (hold_until_full) CHECK(harness.full_seen == 1);
    CHECK(workers_reception_close(&harness.reception));
    CHECK(service_close(&harness.service, NULL));
    int status;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    char run_root[256];
    CHECK(snprintf(run_root, sizeof(run_root), "%s/run", root) > 0);
    CHECK(rmdir(run_root) == 0);
    CHECK(rmdir(root) == 0);
}

static void run_invalid_case(const char *analyst_path,
                              const char *client_path)
{
    char root[] = "/tmp/reception invalid us14 XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    struct harness harness = {0};
    struct service_config config = {.client_count = 1,
                                    .max_patience_ms = 5,
                                    .project_root = root,
                                    .analyst_path = analyst_path};
    CHECK(service_prepare(&harness.service, &config));
    CHECK(client_queue_init(&harness.queue));
    CHECK(metrics_init(&harness.metrics));
    char *client_argv[] = {(char *)client_path, "demand", "bad", NULL};
    CHECK(workers_reception_init(&harness.reception, &harness.service,
                                  &harness.queue, &harness.metrics,
                                  client_path, client_argv, &harness.stop));
    CHECK(scheduler_create(&harness.service.scheduler, 0,
                            workers_reception_run, &harness.reception));
    CHECK(scheduler_create(&harness.service.scheduler, 1,
                            controlled_attendant, &harness));
    CHECK(!scheduler_run_events(&harness.service.scheduler,
                                 service_process_events, service_wait_events,
                                 &harness.service));
    CHECK(harness.reception.error == EINVAL);
    CHECK(harness.queue.count == 0 && harness.queue.reserved);
    char run_directory[256];
    CHECK(snprintf(run_directory, sizeof(run_directory), "%s",
                   harness.service.run_directory) > 0);
    CHECK(workers_reception_close(&harness.reception));
    CHECK(!harness.queue.reserved);
    CHECK(harness.metrics.created == 1 && harness.metrics.cancelled == 1);
    char demand_path[320];
    CHECK(snprintf(demand_path, sizeof(demand_path), "%s/demanda.txt",
                   run_directory) > 0);
    CHECK(access(demand_path, F_OK) == 0);
    CHECK(service_close(&harness.service, NULL));
    CHECK(access(run_directory, F_OK) == -1 && errno == ENOENT);
    int status;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    char run_root[256];
    CHECK(snprintf(run_root, sizeof(run_root), "%s/run", root) > 0);
    CHECK(rmdir(run_root) == 0);
    CHECK(rmdir(root) == 0);
}

int main(int argc, char **argv)
{
    CHECK(argc == 3 && argv[1][0] == '/' && argv[2][0] == '/');
    CHECK(workers_priority_from_bits(0) == CLIENT_PRIORITY_HIGH);
    CHECK(workers_priority_from_bits(1) == CLIENT_PRIORITY_LOW);
    unsigned high = 0;
    unsigned low = 0;
    for (uint64_t bits = 0; bits < 256; bits++) {
        if (workers_priority_from_bits(bits) == CLIENT_PRIORITY_HIGH) high++;
        else low++;
    }
    CHECK(high == 128 && low == 128);
    run_case(argv[1], argv[2], 1, 0, false);
    run_case(argv[1], argv[2], 125, 0, true);
    run_case(argv[1], argv[2], 0, 205, true);
    run_invalid_case(argv[1], argv[2]);
    puts("Recepção: 1, 125 e geração contínua passaram (US14).");
    return EXIT_SUCCESS;
}
