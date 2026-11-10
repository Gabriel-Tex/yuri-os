#ifndef IPC_H
#define IPC_H

#include <stdbool.h>
#include <semaphore.h>

#define IPC_ATTEND_NAME "/sem_atend"
#define IPC_BLOCK_NAME "/sem_block"

/* Inicializar com zeros. O lock fica em /tmp e seu inode não é removido. */
struct ipc_service {
    int lock_fd;
    sem_t *attend;
    sem_t *block;
    bool lock_owned;
    bool attend_owned;
    bool block_owned;
};

enum ipc_semaphore {
    IPC_ATTEND,
    IPC_BLOCK
};

/* O_EXCL impede adotar ou remover semáforos preexistentes. Falha parcial
 * libera somente recursos criados por esta chamada. */
bool ipc_service_init(struct ipc_service *service);
bool ipc_service_lock_init(struct ipc_service *service);
bool ipc_service_create_semaphores(struct ipc_service *service);
bool ipc_service_close(struct ipc_service *service);

/* EAGAIN significa ocupado e retorna sucesso com acquired=false. */
bool ipc_try_acquire(struct ipc_service *service, enum ipc_semaphore which,
                     bool *acquired);
bool ipc_release(struct ipc_service *service, enum ipc_semaphore which);

/* Clientes e Analista abrem os nomes existentes sem adquirir propriedade. */
bool ipc_open_existing(enum ipc_semaphore which, sem_t **handle);
bool ipc_close_existing(sem_t *handle);

#endif
