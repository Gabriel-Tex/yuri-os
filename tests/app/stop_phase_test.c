#define _GNU_SOURCE
#include "app/service.h"
#include "domain/client_queue.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: falhou: %s (errno %d)\n", \
            __FILE__, __LINE__, #condition, errno); exit(EXIT_FAILURE); \
} } while (0)

enum stop_phase {
    STOP_BEFORE_CREATION,
    STOP_DURING_STARTUP,
    STOP_DURING_SERVICE,
    STOP_WITH_FULL_QUEUE,
    STOP_DURING_ANALYSIS,
    STOP_IN_PTY
};

static int run_service(const char *analyst_path, const char *client_path,
                       int ready_fd, enum stop_phase phase)
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
    if (!service_prepare(&service, &config)) return 1;
    if (dprintf(ready_fd, "%s\n", service.run_directory) < 0) return 1;
    close(ready_fd);
    bool ran = service_run_infinite(&service, client_path, &result);
    bool closed = service_close(&service, &result);
    free(root);
    if (!ran || !closed || !result.complete || result.failed != 0 ||
        result.created != result.completed + result.cancelled ||
        result.elapsed_ns == 0 ||
        (phase == STOP_BEFORE_CREATION && result.created != 0) ||
        ((phase == STOP_DURING_SERVICE || phase == STOP_WITH_FULL_QUEUE ||
          phase == STOP_DURING_ANALYSIS) && result.completed == 0)) {
        return 1;
    }
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

static size_t child_count(pid_t pid)
{
    char path[96];
    int length = snprintf(path, sizeof(path), "/proc/%ld/task/%ld/children",
                          (long)pid, (long)pid);
    if (length <= 0 || (size_t)length >= sizeof(path)) return 0;
    FILE *file = fopen(path, "r");
    if (file == NULL) return 0;
    size_t count = 0;
    long child;
    while (fscanf(file, "%ld", &child) == 1) count++;
    fclose(file);
    return count;
}

static bool phase_observed(enum stop_phase phase, pid_t pid,
                           const char *directory, int slave)
{
    char path[4096];
    struct termios attributes;
    switch (phase) {
    case STOP_BEFORE_CREATION:
        return true;
    case STOP_DURING_STARTUP:
        return child_count(pid) >= 2;
    case STOP_DURING_SERVICE:
        return snprintf(path, sizeof(path), "%s/client.active", directory) > 0 &&
               access(path, F_OK) == 0;
    case STOP_WITH_FULL_QUEUE:
        return child_count(pid) >= CLIENT_QUEUE_CAPACITY + 2;
    case STOP_DURING_ANALYSIS:
        return snprintf(path, sizeof(path), "%s/analyst.active", directory) > 0 &&
               access(path, F_OK) == 0;
    case STOP_IN_PTY:
        return tcgetattr(slave, &attributes) == 0 &&
               (attributes.c_lflag & ICANON) == 0;
    }
    return false;
}

static void run_case(const char *analyst_path, const char *slow_analyst,
                     const char *client_path, enum stop_phase phase)
{
    const char *mode = phase == STOP_DURING_STARTUP ? "startup_delay" :
                       (phase == STOP_DURING_SERVICE ||
                        phase == STOP_WITH_FULL_QUEUE) ? "service_slow" : NULL;
    if (mode != NULL) CHECK(setenv("CONTROLLED_CLIENT_MODE", mode, 1) == 0);
    else CHECK(unsetenv("CONTROLLED_CLIENT_MODE") == 0);
    int input[2] = {-1, -1};
    int ready[2];
    int master = -1;
    int slave = -1;
    struct termios original = {0};
    if (phase == STOP_IN_PTY) {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        CHECK(master >= 0 && grantpt(master) == 0 && unlockpt(master) == 0);
        slave = open(ptsname(master), O_RDWR | O_NOCTTY);
        CHECK(slave >= 0 && tcgetattr(slave, &original) == 0);
    } else {
        CHECK(pipe(input) == 0);
    }
    CHECK(pipe(ready) == 0);
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) {
        close(ready[0]);
        if (phase == STOP_IN_PTY) {
            close(master);
            if (dup2(slave, STDIN_FILENO) == -1) _exit(1);
            close(slave);
        } else {
            close(input[1]);
            if (dup2(input[0], STDIN_FILENO) == -1) _exit(1);
            close(input[0]);
        }
        _exit(run_service(phase == STOP_DURING_ANALYSIS ? slow_analyst :
                          analyst_path, client_path, ready[1], phase));
    }
    close(ready[1]);
    if (phase != STOP_IN_PTY) close(input[0]);
    FILE *ready_file = fdopen(ready[0], "r");
    CHECK(ready_file != NULL);
    char directory[4096];
    CHECK(fgets(directory, sizeof(directory), ready_file) != NULL);
    CHECK(fclose(ready_file) == 0);
    size_t length = strlen(directory);
    CHECK(length > 0 && directory[length - 1] == '\n');
    directory[length - 1] = '\0';
    struct timespec pause = {.tv_nsec = 10000000};
    bool observed = false;
    for (int attempt = 0; attempt < 1000; attempt++) {
        if (phase_observed(phase, pid, directory, slave)) {
            observed = true;
            break;
        }
        nanosleep(&pause, NULL);
    }
    CHECK(observed);
    int writer = phase == STOP_IN_PTY ? master : input[1];
    CHECK(write(writer, "s", 1) == 1);
    int status;
    CHECK(waitpid(pid, &status, 0) == pid);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        fprintf(stderr, "fase %d: status %d\n", phase, status);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (phase == STOP_IN_PTY) {
        struct termios restored;
        CHECK(tcgetattr(slave, &restored) == 0);
        CHECK(restored.c_iflag == original.c_iflag &&
              restored.c_oflag == original.c_oflag &&
              restored.c_cflag == original.c_cflag &&
              restored.c_lflag == original.c_lflag &&
              memcmp(restored.c_cc, original.c_cc, NCCS) == 0);
        CHECK(close(slave) == 0);
    } else {
        CHECK(close(input[1]) == 0);
    }
    if (master != -1) CHECK(close(master) == 0);
    CHECK(access(directory, F_OK) == -1 && errno == ENOENT);
}

int main(int argc, char **argv)
{
    CHECK(argc == 4);
    run_case(argv[1], argv[2], argv[3], STOP_BEFORE_CREATION);
    run_case(argv[1], argv[2], argv[3], STOP_DURING_STARTUP);
    run_case(argv[1], argv[2], argv[3], STOP_DURING_SERVICE);
    run_case(argv[1], argv[2], argv[3], STOP_WITH_FULL_QUEUE);
    run_case(argv[1], argv[2], argv[3], STOP_DURING_ANALYSIS);
    run_case(argv[1], argv[2], argv[3], STOP_IN_PTY);
    puts("Parada: criação, inicialização, atendimento, fila cheia, análise e TTY passaram.");
    return EXIT_SUCCESS;
}
