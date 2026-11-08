#define _GNU_SOURCE
#include "platform/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: falhou: %s (errno %d)\n", \
            __FILE__, __LINE__, #condition, errno); exit(EXIT_FAILURE); \
} } while (0)

static void check_absent(const char *name)
{
    sem_t *handle = sem_open(name, 0);
    CHECK(handle == SEM_FAILED && errno == ENOENT);
}

static void check_second_instance(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        struct ipc_service second = {0};
        if (ipc_service_init(&second)) {
            _exit(1);
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            _exit(2);
        }
        _exit(0);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main(void)
{
    /* Não remover um semáforo alheio para preparar o ambiente do teste. */
    check_absent(IPC_ATTEND_NAME);
    check_absent(IPC_BLOCK_NAME);
    struct ipc_service service = {0};
    CHECK(ipc_service_init(&service));
    CHECK(service.lock_owned && service.attend_owned && service.block_owned);
    check_second_instance();
    bool acquired = false;
    CHECK(ipc_try_acquire(&service, IPC_ATTEND, &acquired) && acquired);
    acquired = true;
    CHECK(ipc_try_acquire(&service, IPC_ATTEND, &acquired) && !acquired);
    sem_t *peer = NULL;
    CHECK(ipc_open_existing(IPC_ATTEND, &peer));
    CHECK(sem_trywait(peer) == -1 && errno == EAGAIN);
    CHECK(ipc_release(&service, IPC_ATTEND));
    CHECK(sem_trywait(peer) == 0);
    CHECK(sem_post(peer) == 0);
    CHECK(ipc_close_existing(peer));
    CHECK(ipc_try_acquire(&service, IPC_BLOCK, &acquired) && acquired);
    CHECK(ipc_release(&service, IPC_BLOCK));
    CHECK(!ipc_try_acquire(&service, (enum ipc_semaphore)99, &acquired));
    CHECK(errno == EINVAL);
    CHECK(ipc_service_close(&service));
    CHECK(ipc_service_close(&service));
    check_absent(IPC_ATTEND_NAME);
    check_absent(IPC_BLOCK_NAME);

    /* Recurso preexistente causa erro, e a falha parcial não o remove. */
    sem_t *foreign = sem_open(IPC_BLOCK_NAME, O_CREAT | O_EXCL, 0600, 1);
    CHECK(foreign != SEM_FAILED);
    CHECK(!ipc_service_init(&service) && errno == EEXIST);
    check_absent(IPC_ATTEND_NAME);
    sem_t *observed = sem_open(IPC_BLOCK_NAME, 0);
    CHECK(observed != SEM_FAILED);
    CHECK(sem_close(observed) == 0);
    CHECK(sem_close(foreign) == 0);
    CHECK(sem_unlink(IPC_BLOCK_NAME) == 0);
    CHECK(ipc_service_close(&service));
    CHECK(ipc_service_init(&service));
    CHECK(ipc_service_close(&service));
    check_absent(IPC_ATTEND_NAME);
    check_absent(IPC_BLOCK_NAME);
    int status;
    CHECK(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
    puts("IPC: semáforos, exclusão e limpeza parcial passaram (US09).");
    return EXIT_SUCCESS;
}
