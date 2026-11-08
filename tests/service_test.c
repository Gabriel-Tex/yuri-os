#define _GNU_SOURCE
#include "app/service.h"

#include <errno.h>
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

static void worker(void *argument)
{
    unsigned *steps = argument;
    (*steps)++;
    CHECK(scheduler_yield());
    (*steps)++;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2 && argv[1][0] == '/');
    char root[] = "/tmp/service us13 XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    struct service_config config = {.client_count = 0,
                                    .max_patience_ms = 5,
                                    .project_root = root,
                                    .analyst_path = argv[1]};
    struct service_state service = {0};
    CHECK(service_prepare(&service, &config));
    CHECK(service.initialized && service.analyst_pid > 0);
    CHECK(service.coordinator.state == ANALYST_COORDINATOR_STOPPED);
    CHECK(service.run_directory != NULL);
    struct stat status;
    CHECK(stat(service.run_directory, &status) == 0 && S_ISDIR(status.st_mode));
    unsigned reception_steps = 0;
    unsigned attendant_steps = 0;
    CHECK(scheduler_create(&service.scheduler, 0, worker, &reception_steps));
    CHECK(scheduler_create(&service.scheduler, 1, worker, &attendant_steps));
    CHECK(scheduler_run_events(&service.scheduler, service_process_events,
                                service_wait_events, &service));
    CHECK(reception_steps == 2 && attendant_steps == 2);
    struct service_result result = {0};
    CHECK(service_close(&service, &result));
    CHECK(result.prepared && !result.complete && result.elapsed_ns > 0);
    CHECK(service_close(&service, NULL));
    int child_status;
    CHECK(waitpid(-1, &child_status, WNOHANG) == -1 && errno == ECHILD);

    config.client_count = 3;
    config.analyst_path = "/no/such/analyst";
    CHECK(!service_prepare(&service, &config));
    CHECK(service.run_directory == NULL);
    CHECK(waitpid(-1, &child_status, WNOHANG) == -1 && errno == ECHILD);
    CHECK(service_close(&service, NULL));

    char run_root[256];
    CHECK(snprintf(run_root, sizeof(run_root), "%s/run", root) > 0);
    CHECK(rmdir(run_root) == 0);
    FILE *occupied = fopen(run_root, "w");
    CHECK(occupied != NULL && fclose(occupied) == 0);
    config.analyst_path = argv[1];
    CHECK(!service_prepare(&service, &config) && errno == ENOTDIR);
    CHECK(waitpid(-1, &child_status, WNOHANG) == -1 && errno == ECHILD);
    CHECK(unlink(run_root) == 0);
    CHECK(rmdir(root) == 0);
    puts("Serviço: preparação, contextos e limpeza passaram (US13).");
    return EXIT_SUCCESS;
}
