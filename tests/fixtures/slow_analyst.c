#define _GNU_SOURCE
#include "platform/completion_file.h"
#include "platform/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define ACTIVE_MARKER "analyst.active"

static bool write_pid(int directory_fd)
{
    int descriptor = openat(directory_fd, "analyst.pid",
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor == -1) return false;
    char line[32];
    int length = snprintf(line, sizeof(line), "%ld\n", (long)getpid());
    bool written = length > 0 && (size_t)length < sizeof(line) &&
                   write(descriptor, line, (size_t)length) == length &&
                   fsync(descriptor) == 0;
    int error = errno;
    if (close(descriptor) == -1) written = false;
    if (!written) errno = error;
    return written;
}

int main(int argc, char **argv)
{
    if (argc != 2 || argv[1][0] != '/') return EXIT_FAILURE;
    sem_t *block;
    struct completion_file file = {0};
    if (!ipc_open_existing(IPC_BLOCK, &block)) return EXIT_FAILURE;
    if (!completion_file_open(&file, argv[1], block, false)) {
        ipc_close_existing(block);
        return EXIT_FAILURE;
    }
    if (!write_pid(file.directory_fd)) return EXIT_FAILURE;
    for (;;) {
        if (raise(SIGSTOP) != 0) return EXIT_FAILURE;
        /* O marcador permite enviar s durante um lote em andamento. */
        int marker = openat(file.directory_fd, ACTIVE_MARKER,
                            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (marker == -1 || close(marker) == -1) return EXIT_FAILURE;
        struct timespec pause = {.tv_nsec = 500000000};
        while (nanosleep(&pause, &pause) == -1 && errno == EINTR) {}
        if (!completion_file_lock(&file)) return EXIT_FAILURE;
        size_t printed;
        if (!completion_file_print_batch(&file, STDOUT_FILENO, &printed) ||
            !completion_file_unlock(&file) ||
            unlinkat(file.directory_fd, ACTIVE_MARKER, 0) == -1) {
            return EXIT_FAILURE;
        }
    }
}
