#define _GNU_SOURCE
#include "platform/completion_file.h"
#include "platform/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ANALYST_PID_FILE "analyst.pid"

static bool analyst_write_all(int descriptor, const char *buffer, size_t length)
{
    while (length > 0) {
        ssize_t count = write(descriptor, buffer, length);
        if (count == -1 && errno == EINTR) continue;
        if (count <= 0) {
            if (count == 0) errno = EIO;
            return false;
        }
        buffer += count;
        length -= (size_t)count;
    }
    return true;
}

static bool analyst_write_pid(const char *directory)
{
    int directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd == -1) return false;
    int descriptor = openat(directory_fd, ANALYST_PID_FILE,
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor == -1) {
        int error = errno;
        close(directory_fd);
        errno = error;
        return false;
    }
    char line[32];
    int length = snprintf(line, sizeof(line), "%ld\n", (long)getpid());
    int error = 0;
    if (length < 0 || (size_t)length >= sizeof(line)) {
        error = EOVERFLOW;
    } else if (!analyst_write_all(descriptor, line, (size_t)length)) {
        error = errno;
    } else if (fsync(descriptor) == -1) {
        error = errno;
    }
    if (close(descriptor) == -1 && error == 0) error = errno;
    if (error != 0) unlinkat(directory_fd, ANALYST_PID_FILE, 0);
    if (close(directory_fd) == -1 && error == 0) error = errno;
    if (error != 0) {
        errno = error;
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 2 || argv[1][0] != '/') {
        fprintf(stderr, "analyst: uso: analyst <diretório absoluto>\n");
        return EXIT_FAILURE;
    }
    struct sigaction ignore_pipe = {.sa_handler = SIG_IGN};
    if (sigemptyset(&ignore_pipe.sa_mask) == -1 ||
        sigaction(SIGPIPE, &ignore_pipe, NULL) == -1) {
        perror("analyst: SIGPIPE");
        return EXIT_FAILURE;
    }
    sem_t *block = NULL;
    if (!ipc_open_existing(IPC_BLOCK, &block)) {
        perror("analyst: abrir /sem_block");
        return EXIT_FAILURE;
    }
    struct completion_file file = {0};
    if (!completion_file_open(&file, argv[1], block, false)) {
        perror("analyst: abrir completed.log");
        ipc_close_existing(block);
        return EXIT_FAILURE;
    }
    if (!analyst_write_pid(argv[1])) {
        perror("analyst: gravar analyst.pid");
        completion_file_close(&file);
        ipc_close_existing(block);
        return EXIT_FAILURE;
    }
    for (;;) {
        if (raise(SIGSTOP) != 0) {
            perror("analyst: parar");
            break;
        }
        if (!completion_file_lock(&file)) {
            perror("analyst: proteger arquivo");
            break;
        }
        size_t printed;
        if (!completion_file_print_batch(&file, STDOUT_FILENO, &printed)) {
            perror("analyst: imprimir lote");
            break;
        }
        if (!completion_file_unlock(&file)) {
            perror("analyst: liberar arquivo");
            break;
        }
    }
    completion_file_close(&file);
    ipc_close_existing(block);
    return EXIT_FAILURE;
}
