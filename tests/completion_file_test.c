#define _GNU_SOURCE
#include "platform/completion_file.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: falhou: %s (errno %d)\n", \
            __FILE__, __LINE__, #condition, errno); exit(EXIT_FAILURE); \
} } while (0)

static size_t print_batch(struct completion_file *file, int output_fd)
{
    size_t printed = 99;
    CHECK(completion_file_print_batch(file, output_fd, &printed));
    CHECK(printed <= COMPLETION_BATCH_LIMIT);
    return printed;
}

static void check_count(struct completion_file *file, size_t expected)
{
    size_t count = SIZE_MAX;
    CHECK(completion_file_count(file, &count));
    CHECK(count == expected);
}

static void check_output(int fd, int first, int last)
{
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    FILE *stream = fdopen(dup(fd), "r");
    CHECK(stream != NULL);
    for (int pid = first; pid <= last; pid++) {
        int observed;
        CHECK(fscanf(stream, "%d", &observed) == 1 && observed == pid);
    }
    int extra;
    CHECK(fscanf(stream, "%d", &extra) == EOF);
    CHECK(fclose(stream) == 0);
}

int main(void)
{
    char directory[] = "/tmp/completion_us10_XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    sem_t block;
    CHECK(sem_init(&block, 0, 1) == 0);
    struct completion_file file = {0};
    CHECK(completion_file_open(&file, directory, &block, true));
    bool acquired = false;
    CHECK(sem_trywait(&block) == 0);
    CHECK(completion_file_try_lock(&file, &acquired) && !acquired);
    CHECK(sem_post(&block) == 0);
    CHECK(completion_file_try_lock(&file, &acquired) && acquired);
    int output = openat(file.directory_fd, "output.txt",
                        O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(output >= 0);
    check_count(&file, 0);
    CHECK(print_batch(&file, output) == 0);
    CHECK(!completion_file_append(&file, 0) && errno == EINVAL);

    CHECK(completion_file_append(&file, 1));
    check_count(&file, 1);
    CHECK(print_batch(&file, output) == 1);
    check_count(&file, 0);
    check_output(output, 1, 1);
    CHECK(ftruncate(output, 0) == 0);
    CHECK(lseek(output, 0, SEEK_SET) == 0);

    for (int pid = 1; pid <= 10; pid++) CHECK(completion_file_append(&file, pid));
    CHECK(print_batch(&file, output) == 10);
    check_count(&file, 0);
    check_output(output, 1, 10);
    CHECK(ftruncate(output, 0) == 0);
    CHECK(lseek(output, 0, SEEK_SET) == 0);

    for (int pid = 1; pid <= 11; pid++) CHECK(completion_file_append(&file, pid));
    CHECK(print_batch(&file, output) == 10);
    check_count(&file, 1);
    CHECK(print_batch(&file, output) == 1);
    check_count(&file, 0);
    check_output(output, 1, 11);
    CHECK(ftruncate(output, 0) == 0);
    CHECK(lseek(output, 0, SEEK_SET) == 0);

    for (int pid = 1; pid <= 27; pid++) CHECK(completion_file_append(&file, pid));
    CHECK(print_batch(&file, output) == 10);
    CHECK(print_batch(&file, output) == 10);
    CHECK(print_batch(&file, output) == 7);
    check_count(&file, 0);
    check_output(output, 1, 27);

    CHECK(completion_file_append(&file, 42));
    int full = open("/dev/full", O_WRONLY | O_CLOEXEC);
    CHECK(full >= 0);
    size_t printed = 99;
    CHECK(!completion_file_print_batch(&file, full, &printed));
    CHECK(errno == ENOSPC && printed == 0);
    check_count(&file, 1);
    CHECK(close(full) == 0);
    CHECK(print_batch(&file, output) == 1);
    check_count(&file, 0);

    struct completion_file peer = {0};
    CHECK(completion_file_open(&peer, directory, &block, false));
    CHECK(completion_file_unlock(&file));
    CHECK(completion_file_lock(&peer));
    CHECK(completion_file_append(&peer, 88));
    CHECK(completion_file_unlock(&peer));
    CHECK(completion_file_try_lock(&file, &acquired) && acquired);
    check_count(&file, 1);
    CHECK(print_batch(&file, output) == 1);
    check_count(&file, 0);
    CHECK(completion_file_close(&peer));

    int corrupt = openat(file.directory_fd, COMPLETION_FILE_NAME,
                         O_WRONLY | O_APPEND | O_CLOEXEC);
    CHECK(corrupt >= 0);
    CHECK(write(corrupt, "bad\n", 4) == 4);
    CHECK(close(corrupt) == 0);
    size_t unchanged = 99;
    CHECK(!completion_file_count(&file, &unchanged) && errno == EINVAL);
    CHECK(unchanged == 99);
    CHECK(!completion_file_print_batch(&file, output, &unchanged));
    CHECK(errno == EINVAL && unchanged == 99);
    CHECK(lseek(file.file_fd, 0, SEEK_END) == 4);

    CHECK(completion_file_unlock(&file));
    CHECK(!completion_file_append(&file, 5) && errno == EINVAL);
    CHECK(completion_file_close(&file));
    CHECK(completion_file_close(&file));
    CHECK(close(output) == 0);
    CHECK(sem_destroy(&block) == 0);
    char path[256];
    CHECK(snprintf(path, sizeof(path), "%s/%s", directory,
                   COMPLETION_FILE_NAME) > 0);
    CHECK(unlink(path) == 0);
    CHECK(snprintf(path, sizeof(path), "%s/output.txt", directory) > 0);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    puts("Arquivo de PIDs: lotes e falha de saída passaram (US10).");
    return EXIT_SUCCESS;
}
