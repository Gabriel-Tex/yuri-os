#define _GNU_SOURCE
#include "app/workers.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

struct start_observation {
    pid_t pid;
    bool stopped;
    bool failed;
};

static void observe_start(void *context, const struct processes_event *event)
{
    struct start_observation *observation = context;
    if (event->pid != observation->pid) {
        observation->failed = true;
    } else if (event->kind == PROCESSES_STOPPED && event->code == SIGSTOP) {
        observation->stopped = true;
    } else {
        observation->failed = true;
    }
}

static void idle_reception(void *argument)
{
    (void)argument;
    assert(scheduler_yield());
}

static void run_case(const char *analyst_path, const char *client_path,
                     const char *mode, bool expected_failure)
{
    struct service_state service = {0};
    char *project_root = getcwd(NULL, 0);
    assert(project_root != NULL);
    struct service_config config = {.client_count = 1,
        .max_patience_ms = expected_failure ? 1000 : 1,
        .project_root = project_root,
        .analyst_path = analyst_path};
    assert(service_prepare(&service, &config));
    assert(ipc_service_create_semaphores(&service.ipc));
    assert(service_start_analyst(&service));
    while (!service_analyst_ready(&service)) {
        assert(service_process_events(&service));
        if (!service_analyst_ready(&service)) assert(service_wait_events(&service));
    }
    struct client_queue queue;
    struct metrics metrics;
    assert(client_queue_init(&queue) && metrics_init(&metrics));
    bool generation_finished = true;
    struct workers_attendant attendant;
    assert(workers_attendant_init(&attendant, &service, &queue, &metrics,
                                  &generation_finished));
    char *arguments[] = {(char *)client_path,
        (char *)mode, NULL};
    pid_t pid;
    assert(processes_spawn(&service.watcher, client_path, arguments,
                           service.run_directory, &pid));
    struct start_observation observation = {.pid = pid};
    while (!observation.stopped && !observation.failed) {
        assert(processes_collect(&service.watcher, observe_start,
                                 &observation, NULL));
        if (!observation.stopped && !observation.failed)
            assert(processes_wait(&service.watcher));
    }
    assert(observation.stopped && !observation.failed);
    struct client_record client;
    assert(metrics_record_created(&metrics));
    assert(client_init(&client, pid, 0, CLIENT_PRIORITY_HIGH, 0,
                       config.max_patience_ms));
    assert(client_enqueue(&client, 1000));
    assert(client_queue_reserve(&queue));
    assert(client_queue_insert(&queue, &client));
    service.client_handler = workers_attendant_on_event;
    service.client_context = &attendant;
    assert(scheduler_create(&service.scheduler, 0, idle_reception, NULL));
    assert(scheduler_create(&service.scheduler, 1, workers_attendant_run,
                            &attendant));
    bool ran = scheduler_run_events(&service.scheduler, service_process_events,
                                    service_wait_events, &service);
    if (expected_failure) {
        assert(!ran && attendant.active_reaped);
        assert(workers_attendant_close(&attendant));
        assert(metrics.failed == 1 && metrics.completed == 0);
    } else {
        assert(ran && attendant.finished);
        assert(metrics.completed == 1 && metrics.satisfied == 0);
        assert(workers_attendant_close(&attendant));
        assert(analyst_coordinator_request_drain(&service.coordinator));
        bool drained = false;
        while (!drained) {
            assert(service_process_events(&service));
            assert(analyst_coordinator_is_drained(&service.coordinator,
                                                 &drained));
            if (!drained) assert(service_wait_events(&service));
        }
    }
    struct service_result result = {0};
    char demand_path[4096];
    int length = snprintf(demand_path, sizeof(demand_path), "%s/demanda.txt",
                          service.run_directory);
    assert(length > 0 && (size_t)length < sizeof(demand_path));
    assert(unlink(demand_path) == 0);
    if (!service_close(&service, &result)) {
        perror("service_close");
        assert(false);
    }
    free(project_root);
    errno = 0;
    assert(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    run_case(argv[1], argv[2], "service_late", false);
    run_case(argv[1], argv[2], "service_signal", true);
    run_case(argv[1], argv[2], "service_hold", true);
    puts("Atendente: conclusão tardia, falha por sinal e semáforo ocupado confirmados.");
    return 0;
}
