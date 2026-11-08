#define _GNU_SOURCE
#include "app/service.h"

#include <assert.h>
#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    assert(argc == 3);
    char *root = getcwd(NULL, 0);
    assert(root != NULL);
    struct service_config config = {.client_count = 11,
        .max_patience_ms = 10000, .project_root = root,
        .analyst_path = argv[1]};
    struct service_state service = {0};
    struct service_result result = {0};
    assert(service_prepare(&service, &config));
    assert(service_run_finite(&service, argv[2], &result));
    assert(result.complete && result.created == 11 &&
           result.completed == 11 && result.cancelled == 0 &&
           result.failed == 0 && result.satisfied <= 11);
    assert(service_close(&service, &result));
    assert(result.complete && result.elapsed_ns > 0);
    errno = 0;
    assert(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    errno = 0;
    assert(sem_open(IPC_ATTEND_NAME, 0) == SEM_FAILED && errno == ENOENT);
    errno = 0;
    assert(sem_open(IPC_BLOCK_NAME, 0) == SEM_FAILED && errno == ENOENT);

    /* Uma falha de exec após o fork não pode produzir relatório completo. */
    service = (struct service_state){0};
    result = (struct service_result){0};
    config.client_count = 1;
    assert(service_prepare(&service, &config));
    assert(!service_run_finite(&service, "/tmp/cliente-inexistente-us16",
                               &result));
    assert(!result.complete && result.completed == 0);
    assert(service_close(&service, &result));
    errno = 0;
    assert(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    free(root);
    puts("Serviço finito: 11 conclusões, drenagem e limpeza passaram.");
    return 0;
}
