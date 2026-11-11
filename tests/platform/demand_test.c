#define _GNU_SOURCE
#include "platform/demand.h"
#include "platform/clock.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: falhou: %s (errno %d)\n", \
            __FILE__, __LINE__, #condition, errno); exit(EXIT_FAILURE); \
} } while (0)

struct event_log {
    struct processes_event events[8];
    size_t count;
};

static void record_event(void *context, const struct processes_event *event)
{
    struct event_log *log = context;
    CHECK(log->count < 8);
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
        }
        log.count = 0;
        CHECK(processes_wait(watcher));
    }
}

static pid_t start(struct demand_startup *startup,
                   struct processes_watcher *watcher, const char *path,
                   const char *directory, const char *mode, const char *value,
                   uint64_t epoch)
{
    char *arguments[] = {(char *)path, (char *)mode, (char *)value, NULL};
    pid_t pid = -1;
    uint64_t arrival = UINT64_MAX;
    CHECK(demand_begin(startup, watcher, path, arguments, directory, epoch,
                       &pid, &arrival));
    uint64_t now;
    CHECK(clock_monotonic_ns(&now));
    CHECK(pid > 0 && arrival <= now - epoch);
    return pid;
}

static void finish(struct processes_watcher *watcher, pid_t pid)
{
    CHECK(processes_send(pid, SIGCONT));
    CHECK(wait_event(watcher, pid, PROCESSES_EXITED_OK).code == 0);
    int status;
    CHECK(waitpid(pid, &status, WNOHANG) == -1 && errno == ECHILD);
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && argv[1][0] == '/');
    char directory[] = "/tmp/demand_us08_XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    struct demand_startup startup = {0};
    struct processes_watcher watcher = {0};
    CHECK(demand_init(&startup, directory));
    CHECK(processes_watcher_init(&watcher));
    uint64_t epoch;
    CHECK(clock_monotonic_ns(&epoch));

    pid_t first = start(&startup, &watcher, argv[1], directory,
                        "demand_delayed", "123", epoch);
    char *arguments[] = {argv[1], "demand", "456", NULL};
    pid_t refused = -1;
    uint64_t refused_arrival = 99;
    CHECK(!demand_begin(&startup, &watcher, argv[1], arguments, directory,
                        epoch, &refused, &refused_arrival));
    CHECK(errno == EINVAL && refused == -1 && refused_arrival == 99);
    uint64_t demand = 77;
    CHECK(!demand_consume(&startup, &demand));
    CHECK(errno == EINVAL && demand == 77);
    struct processes_event unrelated = {.pid = first + 1000,
                                        .kind = PROCESSES_STOPPED,
                                        .code = SIGSTOP};
    CHECK(!demand_observe(&startup, &unrelated) && errno == EINVAL);
    struct processes_event event = wait_event(&watcher, first, PROCESSES_STOPPED);
    CHECK(demand_observe(&startup, &event));
    CHECK(demand_consume(&startup, &demand) && demand == 123);
    finish(&watcher, first);

    pid_t second = start(&startup, &watcher, argv[1], directory,
                         "demand", "456", epoch);
    event = wait_event(&watcher, second, PROCESSES_STOPPED);
    CHECK(demand_observe(&startup, &event));
    CHECK(demand_consume(&startup, &demand) && demand == 456);
    finish(&watcher, second);

    pid_t invalid = start(&startup, &watcher, argv[1], directory,
                          "demand", "-2", epoch);
    event = wait_event(&watcher, invalid, PROCESSES_STOPPED);
    CHECK(demand_observe(&startup, &event));
    CHECK(!demand_consume(&startup, &demand) && errno == EINVAL);
    CHECK(demand == 456);
    CHECK(!demand_begin(&startup, &watcher, argv[1], arguments, directory,
                        epoch, &refused, &refused_arrival));
    demand_abort(&startup);
    finish(&watcher, invalid);
    char file_path[256];
    CHECK(snprintf(file_path, sizeof(file_path), "%s/demanda.txt", directory) > 0);
    CHECK(unlink(file_path) == 0);

    pid_t missing = start(&startup, &watcher, argv[1], directory,
                          "demand_missing", NULL, epoch);
    event = wait_event(&watcher, missing, PROCESSES_STOPPED);
    CHECK(demand_observe(&startup, &event));
    CHECK(!demand_consume(&startup, &demand) && errno == ENOENT);
    demand_abort(&startup);
    finish(&watcher, missing);

    pid_t early_exit = start(&startup, &watcher, argv[1], directory,
                             "exit0", NULL, epoch);
    event = wait_event(&watcher, early_exit, PROCESSES_EXITED_OK);
    CHECK(!demand_observe(&startup, &event) && errno == ECHILD);

    FILE *stale = fopen(file_path, "w");
    CHECK(stale != NULL && fputs("9", stale) != EOF && fclose(stale) == 0);
    CHECK(!demand_begin(&startup, &watcher, argv[1], arguments, directory,
                        epoch, &refused, &refused_arrival));
    CHECK(errno == EEXIST);
    CHECK(unlink(file_path) == 0);
    CHECK(processes_watcher_close(&watcher));
    CHECK(demand_close(&startup));
    CHECK(demand_close(&startup));
    CHECK(rmdir(directory) == 0);
    puts("Demanda: criação serial, parada e validação passaram (US08).");
    return EXIT_SUCCESS;
}
