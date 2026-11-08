#define _GNU_SOURCE
#include "app/service.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SERVICE_X_MAX_MS (UINT64_MAX / UINT64_C(1000000))

static bool service_parse_integer(const char *text, bool allow_zero,
                                  uint64_t maximum, uint64_t *value)
{
    if (text == NULL || text[0] == '\0' || value == NULL) return false;
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != '\0'; cursor++) {
        if (*cursor < '0' || *cursor > '9') return false;
    }
    errno = 0;
    char *end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > maximum ||
        (!allow_zero && parsed == 0)) return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool service_find_paths(char **root_out, char **analyst_out)
{
    char *executable = realpath("/proc/self/exe", NULL);
    if (executable == NULL) return false;
    char *slash = strrchr(executable, '/');
    if (slash == NULL) {
        free(executable);
        errno = EINVAL;
        return false;
    }
    size_t directory_length = (size_t)(slash - executable);
    char *analyst = malloc(directory_length + sizeof("/analyst"));
    if (analyst == NULL) {
        free(executable);
        return false;
    }
    memcpy(analyst, executable, directory_length);
    memcpy(analyst + directory_length, "/analyst", sizeof("/analyst"));
    *slash = '\0';
    char *root = NULL;
    for (;;) {
        size_t length = strlen(executable);
        if (length > SIZE_MAX - sizeof("/docs/architecture.md")) break;
        char *marker = malloc(length + sizeof("/docs/architecture.md"));
        if (marker == NULL) break;
        memcpy(marker, executable, length);
        memcpy(marker + length, "/docs/architecture.md",
               sizeof("/docs/architecture.md"));
        struct stat status;
        bool found = stat(marker, &status) == 0 && S_ISREG(status.st_mode);
        free(marker);
        if (found) {
            root = strdup(executable);
            break;
        }
        slash = strrchr(executable, '/');
        if (slash == NULL || slash == executable) break;
        *slash = '\0';
    }
    free(executable);
    if (root == NULL) {
        free(analyst);
        errno = ENOENT;
        return false;
    }
    *root_out = root;
    *analyst_out = analyst;
    return true;
}

static char *service_find_client(const char *analyst)
{
    const char *override = getenv("SERVICE_CLIENT_PATH");
    if (override != NULL && override[0] != '\0') {
        if (override[0] != '/') {
            errno = EINVAL;
            return NULL;
        }
        return realpath(override, NULL);
    }
    const char *slash = strrchr(analyst, '/');
    if (slash == NULL) {
        errno = EINVAL;
        return NULL;
    }
    size_t directory_length = (size_t)(slash - analyst);
    char *candidate = malloc(directory_length + sizeof("/client"));
    if (candidate == NULL) return NULL;
    memcpy(candidate, analyst, directory_length);
    memcpy(candidate + directory_length, "/client", sizeof("/client"));
    char *client = realpath(candidate, NULL);
    int error = errno;
    free(candidate);
    errno = error;
    return client;
}

struct service_stub_steps {
    unsigned reception;
    unsigned attendant;
    bool failed;
};

static void service_reception_stub(void *argument)
{
    struct service_stub_steps *steps = argument;
    steps->reception++;
    if (!scheduler_yield()) {
        steps->failed = true;
        return;
    }
    steps->reception++;
}

static void service_attendant_stub(void *argument)
{
    struct service_stub_steps *steps = argument;
    steps->attendant++;
    if (!scheduler_yield()) {
        steps->failed = true;
        return;
    }
    steps->attendant++;
}

int main(int argc, char **argv)
{
    uint64_t count;
    uint64_t patience_ms;
    if (argc != 3 || !service_parse_integer(argv[1], true, UINT64_MAX,
                                             &count) ||
        !service_parse_integer(argv[2], false, SERVICE_X_MAX_MS,
                               &patience_ms)) {
        fprintf(stderr, "uso: service N X (N >= 0, X > 0 em ms)\n");
        return EXIT_FAILURE;
    }
    char *root = NULL;
    char *analyst = NULL;
    if (!service_find_paths(&root, &analyst)) {
        perror("service: localizar executáveis e projeto");
        return EXIT_FAILURE;
    }
    struct service_config config = {.client_count = count,
                                    .max_patience_ms = patience_ms,
                                    .project_root = root,
                                    .analyst_path = analyst};
    if (count > 0) {
        char *client_path = service_find_client(analyst);
        if (client_path == NULL || access(client_path, X_OK) == -1) {
            int error = errno;
            free(client_path);
            free(root);
            free(analyst);
            errno = error;
            perror("service: cliente não disponível");
            return EXIT_FAILURE;
        }
        struct service_state finite_service = {0};
        struct service_result finite_result = {0};
        bool prepared_finite = service_prepare(&finite_service, &config);
        int error = prepared_finite ? 0 : errno;
        bool ran_finite = false;
        if (prepared_finite) {
            ran_finite = service_run_finite(&finite_service, client_path,
                                            &finite_result);
            if (!ran_finite) error = errno;
        }
        bool closed_finite = service_close(&finite_service, &finite_result);
        if (!closed_finite && error == 0) error = errno;
        free(client_path);
        free(root);
        free(analyst);
        if (!prepared_finite || !ran_finite || !closed_finite ||
            !finite_result.complete) {
            errno = error != 0 ? error : EPROTO;
            perror("service: execução finita");
            if (prepared_finite && !ran_finite) {
                fprintf(stderr, "service: execução parcial: criados=%" PRIu64
                        ", concluídos=%" PRIu64 ", cancelados=%" PRIu64
                        ", falhos=%" PRIu64 "\n", finite_result.created,
                        finite_result.completed, finite_result.cancelled,
                        finite_result.failed);
            }
            return EXIT_FAILURE;
        }
        printf("criados: %" PRIu64 "\nconcluídos: %" PRIu64
               "\nsatisfeitos: %" PRIu64 "\ninsatisfeitos: %" PRIu64
               "\ntaxa de satisfação: %" PRIu64 "/%" PRIu64 " (%.2f%%)\n"
               "tempo total: %.3f ms\n",
               finite_result.created, finite_result.completed,
               finite_result.satisfied,
               finite_result.completed - finite_result.satisfied,
               finite_result.satisfied, finite_result.created,
               100.0 * (double)finite_result.satisfied /
                   (double)finite_result.created,
               (double)finite_result.elapsed_ns / 1000000.0);
        return fflush(stdout) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    struct service_state service = {0};
    struct service_result result = {0};
    bool prepared = service_prepare(&service, &config);
    int preparation_error = prepared ? 0 : errno;
    if (prepared) {
        struct service_stub_steps steps = {0};
        if (!scheduler_create(&service.scheduler, 0,
                              service_reception_stub, &steps) ||
            !scheduler_create(&service.scheduler, 1,
                              service_attendant_stub, &steps) ||
            !scheduler_run_events(&service.scheduler, service_process_events,
                                  service_wait_events, &service) ||
            steps.failed || steps.reception != 2 || steps.attendant != 2) {
            preparation_error = steps.failed || steps.reception != 2 ||
                                steps.attendant != 2 ? EPROTO : errno;
            prepared = false;
        }
    }
    bool closed = service_close(&service, &result);
    int cleanup_error = errno;
    free(root);
    free(analyst);
    if (!prepared) {
        errno = preparation_error;
        perror("service: preparar execução");
        return EXIT_FAILURE;
    }
    if (!closed) {
        errno = cleanup_error;
        perror("service: liberar execução");
        return EXIT_FAILURE;
    }
    /* A CLI não informa sucesso acadêmico antes de integrar o atendimento. */
    fprintf(stderr, "service: entrada aceita e recursos preparados; modo infinito pendente (US17–US18)\n");
    return 2;
}
