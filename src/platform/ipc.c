#define _GNU_SOURCE
#include "ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define IPC_LOCK_PATH_SIZE 96

static const char *ipc_name(enum ipc_semaphore which)
{
    switch (which) {
    case IPC_ATTEND:
        return IPC_ATTEND_NAME;
    case IPC_BLOCK:
        return IPC_BLOCK_NAME;
    }
    return NULL;
}

static sem_t *ipc_owned_handle(struct ipc_service *service,
                               enum ipc_semaphore which)
{
    if (service == NULL || !service->lock_owned) {
        return NULL;
    }
    switch (which) {
    case IPC_ATTEND:
        return service->attend;
    case IPC_BLOCK:
        return service->block;
    }
    return NULL;
}

bool ipc_service_lock_init(struct ipc_service *service)
{
    if (service == NULL || service->lock_owned || service->attend_owned ||
        service->block_owned || service->attend != NULL ||
        service->block != NULL) {
        errno = EINVAL;
        return false;
    }
    char path[IPC_LOCK_PATH_SIZE];
    int length = snprintf(path, sizeof(path), "/tmp/service-simulator-%lu.lock",
                          (unsigned long)getuid());
    if (length < 0 || (size_t)length >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return false;
    }
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd == -1) {
        return false;
    }
    struct stat status;
    if (fstat(fd, &status) == -1) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return false;
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != getuid()) {
        close(fd);
        errno = EACCES;
        return false;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) == -1) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return false;
    }
    service->lock_fd = fd;
    service->lock_owned = true;
    return true;
}

bool ipc_service_create_semaphores(struct ipc_service *service)
{
    if (service == NULL || !service->lock_owned || service->attend != NULL ||
        service->block != NULL || service->attend_owned || service->block_owned) {
        errno = EINVAL;
        return false;
    }
    service->attend = sem_open(IPC_ATTEND_NAME, O_CREAT | O_EXCL, 0600, 1);
    if (service->attend == SEM_FAILED) {
        service->attend = NULL;
        int saved_errno = errno;
        ipc_service_close(service);
        errno = saved_errno;
        return false;
    }
    service->attend_owned = true;
    service->block = sem_open(IPC_BLOCK_NAME, O_CREAT | O_EXCL, 0600, 1);
    if (service->block == SEM_FAILED) {
        service->block = NULL;
        int saved_errno = errno;
        ipc_service_close(service);
        errno = saved_errno;
        return false;
    }
    service->block_owned = true;
    return true;
}

bool ipc_service_init(struct ipc_service *service)
{
    return ipc_service_lock_init(service) &&
           ipc_service_create_semaphores(service);
}

bool ipc_service_close(struct ipc_service *service)
{
    if (service == NULL) {
        errno = EINVAL;
        return false;
    }
    int first_error = 0;
    if (service->block != NULL) {
        if (sem_close(service->block) == -1) {
            first_error = errno;
        } else {
            service->block = NULL;
        }
    }
    if (service->block_owned) {
        if (sem_unlink(IPC_BLOCK_NAME) == -1) {
            if (first_error == 0) first_error = errno;
        } else {
            service->block_owned = false;
        }
    }
    if (service->attend != NULL) {
        if (sem_close(service->attend) == -1) {
            if (first_error == 0) first_error = errno;
        } else {
            service->attend = NULL;
        }
    }
    if (service->attend_owned) {
        if (sem_unlink(IPC_ATTEND_NAME) == -1) {
            if (first_error == 0) first_error = errno;
        } else {
            service->attend_owned = false;
        }
    }
    if (service->lock_owned) {
        if (flock(service->lock_fd, LOCK_UN) == -1) {
            if (first_error == 0) first_error = errno;
        }
        if (close(service->lock_fd) == -1) {
            if (first_error == 0) first_error = errno;
        }
        service->lock_owned = false;
    }
    if (first_error != 0) {
        errno = first_error;
        return false;
    }
    return true;
}

bool ipc_try_acquire(struct ipc_service *service, enum ipc_semaphore which,
                     bool *acquired)
{
    sem_t *handle = ipc_owned_handle(service, which);
    if (handle == NULL || acquired == NULL) {
        errno = EINVAL;
        return false;
    }
    if (sem_trywait(handle) == 0) {
        *acquired = true;
        return true;
    }
    if (errno == EAGAIN) {
        *acquired = false;
        return true;
    }
    return false;
}

bool ipc_release(struct ipc_service *service, enum ipc_semaphore which)
{
    sem_t *handle = ipc_owned_handle(service, which);
    if (handle == NULL) {
        errno = EINVAL;
        return false;
    }
    return sem_post(handle) == 0;
}

bool ipc_open_existing(enum ipc_semaphore which, sem_t **handle)
{
    const char *name = ipc_name(which);
    if (name == NULL || handle == NULL) {
        errno = EINVAL;
        return false;
    }
    sem_t *opened = sem_open(name, 0);
    if (opened == SEM_FAILED) {
        return false;
    }
    *handle = opened;
    return true;
}

bool ipc_close_existing(sem_t *handle)
{
    if (handle == NULL || handle == SEM_FAILED) {
        errno = EINVAL;
        return false;
    }
    return sem_close(handle) == 0;
}
