#define _GNU_SOURCE
#include "completion_file.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COMPLETION_TEMP_NAME_SIZE 96

struct completion_scan {
    pid_t first[COMPLETION_BATCH_LIMIT];
    off_t end_offsets[COMPLETION_BATCH_LIMIT];
    size_t total;
};

/* renameat troca o inode visível pelo nome. Outro processo pode manter um
 * descritor antigo; reabrir sob o semáforo observa sempre o arquivo atual. */
static bool completion_refresh(struct completion_file *file)
{
    int current = openat(file->directory_fd, COMPLETION_FILE_NAME,
                         O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (current == -1) return false;
    struct stat status;
    if (fstat(current, &status) == -1) {
        int error = errno;
        close(current);
        errno = error;
        return false;
    }
    if (!S_ISREG(status.st_mode)) {
        close(current);
        errno = EINVAL;
        return false;
    }
    int previous = file->file_fd;
    file->file_fd = current;
    if (close(previous) == -1) return false;
    return true;
}

static bool completion_write_all(int fd, const char *buffer, size_t length)
{
    while (length > 0) {
        ssize_t written = write(fd, buffer, length);
        if (written == -1 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            if (written == 0) errno = EIO;
            return false;
        }
        buffer += written;
        length -= (size_t)written;
    }
    return true;
}

static bool completion_scan_file(struct completion_file *file,
                                 struct completion_scan *scan)
{
    if (lseek(file->file_fd, 0, SEEK_SET) == (off_t)-1) {
        return false;
    }
    *scan = (struct completion_scan){0};
    char buffer[4096];
    uint64_t value = 0;
    bool has_digit = false;
    off_t offset = 0;
    for (;;) {
        ssize_t count = read(file->file_fd, buffer, sizeof(buffer));
        if (count == -1 && errno == EINTR) {
            continue;
        }
        if (count == -1) {
            return false;
        }
        if (count == 0) {
            break;
        }
        for (ssize_t i = 0; i < count; i++) {
            char character = buffer[i];
            if (offset == (off_t)INT64_MAX) {
                errno = EOVERFLOW;
                return false;
            }
            offset++;
            if (character >= '0' && character <= '9') {
                unsigned digit = (unsigned)(character - '0');
                if (value > ((uint64_t)INT_MAX - digit) / 10) {
                    errno = ERANGE;
                    return false;
                }
                value = value * 10 + digit;
                has_digit = true;
            } else if (character == '\n' && has_digit && value > 0) {
                if (scan->total == SIZE_MAX) {
                    errno = EOVERFLOW;
                    return false;
                }
                if (scan->total < COMPLETION_BATCH_LIMIT) {
                    scan->first[scan->total] = (pid_t)value;
                    scan->end_offsets[scan->total] = offset;
                }
                scan->total++;
                value = 0;
                has_digit = false;
            } else {
                errno = EINVAL;
                return false;
            }
        }
    }
    if (has_digit) {
        errno = EINVAL;
        return false;
    }
    return true;
}

static bool completion_rewrite_suffix(struct completion_file *file,
                                      off_t prefix_end)
{
    char temporary_name[COMPLETION_TEMP_NAME_SIZE];
    int temporary_fd = -1;
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        int length = snprintf(temporary_name, sizeof(temporary_name),
                              ".completed.%ld.%llu.tmp", (long)getpid(),
                              (unsigned long long)file->temporary_sequence++);
        if (length < 0 || (size_t)length >= sizeof(temporary_name)) {
            errno = ENAMETOOLONG;
            return false;
        }
        temporary_fd = openat(file->directory_fd, temporary_name,
                              O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (temporary_fd != -1) {
            break;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    if (temporary_fd == -1) {
        errno = EEXIST;
        return false;
    }
    int error = 0;
    if (lseek(file->file_fd, prefix_end, SEEK_SET) == (off_t)-1) {
        error = errno;
    }
    char buffer[4096];
    while (error == 0) {
        ssize_t count = read(file->file_fd, buffer, sizeof(buffer));
        if (count == -1 && errno == EINTR) {
            continue;
        }
        if (count == -1) {
            error = errno;
            break;
        }
        if (count == 0) {
            break;
        }
        if (!completion_write_all(temporary_fd, buffer, (size_t)count)) {
            error = errno;
        }
    }
    if (error == 0 && fsync(temporary_fd) == -1) {
        error = errno;
    }
    if (error == 0 && renameat(file->directory_fd, temporary_name,
                                file->directory_fd, COMPLETION_FILE_NAME) == -1) {
        error = errno;
    }
    if (error != 0) {
        close(temporary_fd);
        unlinkat(file->directory_fd, temporary_name, 0);
        errno = error;
        return false;
    }
    int old_fd = file->file_fd;
    file->file_fd = temporary_fd;
    if (close(old_fd) == -1) {
        return false;
    }
    return true;
}

bool completion_file_open(struct completion_file *file, const char *directory,
                          sem_t *block, bool create)
{
    if (file == NULL || file->initialized || directory == NULL ||
        directory[0] != '/' || block == NULL || block == SEM_FAILED) {
        errno = EINVAL;
        return false;
    }
    int directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd == -1) {
        return false;
    }
    int flags = O_RDWR | O_CLOEXEC | O_NOFOLLOW;
    if (create) flags |= O_CREAT | O_EXCL;
    int file_fd = openat(directory_fd, COMPLETION_FILE_NAME, flags, 0600);
    if (file_fd == -1) {
        int error = errno;
        close(directory_fd);
        errno = error;
        return false;
    }
    struct stat status;
    if (fstat(file_fd, &status) == -1) {
        int error = errno;
        close(file_fd);
        close(directory_fd);
        errno = error;
        return false;
    }
    if (!S_ISREG(status.st_mode)) {
        close(file_fd);
        close(directory_fd);
        errno = EINVAL;
        return false;
    }
    *file = (struct completion_file){.directory_fd = directory_fd,
                                     .file_fd = file_fd, .block = block,
                                     .initialized = true};
    return true;
}

bool completion_file_close(struct completion_file *file)
{
    if (file == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!file->initialized) {
        return true;
    }
    int error = 0;
    if (file->locked && !completion_file_unlock(file)) {
        error = errno;
    }
    if (close(file->file_fd) == -1 && error == 0) error = errno;
    if (close(file->directory_fd) == -1 && error == 0) error = errno;
    file->initialized = false;
    file->locked = false;
    file->block = NULL;
    if (error != 0) {
        errno = error;
        return false;
    }
    return true;
}

bool completion_file_try_lock(struct completion_file *file, bool *acquired)
{
    if (file == NULL || !file->initialized || file->locked || acquired == NULL) {
        errno = EINVAL;
        return false;
    }
    if (sem_trywait(file->block) == 0) {
        file->locked = true;
        *acquired = true;
        return true;
    }
    if (errno == EAGAIN) {
        *acquired = false;
        return true;
    }
    return false;
}

bool completion_file_lock(struct completion_file *file)
{
    if (file == NULL || !file->initialized || file->locked) {
        errno = EINVAL;
        return false;
    }
    int result;
    do {
        result = sem_wait(file->block);
    } while (result == -1 && errno == EINTR);
    if (result == -1) return false;
    file->locked = true;
    return true;
}

bool completion_file_unlock(struct completion_file *file)
{
    if (file == NULL || !file->initialized || !file->locked) {
        errno = EINVAL;
        return false;
    }
    if (sem_post(file->block) == -1) return false;
    file->locked = false;
    return true;
}

bool completion_file_append(struct completion_file *file, pid_t pid)
{
    if (file == NULL || !file->initialized || !file->locked || pid <= 0) {
        errno = EINVAL;
        return false;
    }
    if (!completion_refresh(file)) return false;
    off_t original_end = lseek(file->file_fd, 0, SEEK_END);
    if (original_end == (off_t)-1) return false;
    char line[32];
    int length = snprintf(line, sizeof(line), "%ld\n", (long)pid);
    if (length < 0 || (size_t)length >= sizeof(line)) {
        errno = EOVERFLOW;
        return false;
    }
    if (!completion_write_all(file->file_fd, line, (size_t)length)) {
        int error = errno;
        if (ftruncate(file->file_fd, original_end) == -1) return false;
        errno = error;
        return false;
    }
    return true;
}

bool completion_file_count(struct completion_file *file, size_t *count)
{
    if (file == NULL || !file->initialized || !file->locked || count == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!completion_refresh(file)) return false;
    struct completion_scan scan;
    if (!completion_scan_file(file, &scan)) return false;
    *count = scan.total;
    return true;
}

bool completion_file_print_batch(struct completion_file *file, int output_fd,
                                 size_t *printed)
{
    if (file == NULL || !file->initialized || !file->locked ||
        output_fd < 0 || printed == NULL) {
        errno = EINVAL;
        return false;
    }
    if (!completion_refresh(file)) return false;
    struct completion_scan scan;
    if (!completion_scan_file(file, &scan)) return false;
    size_t limit = scan.total < COMPLETION_BATCH_LIMIT ? scan.total :
                   COMPLETION_BATCH_LIMIT;
    size_t confirmed = 0;
    int output_error = 0;
    for (; confirmed < limit; confirmed++) {
        char line[32];
        int length = snprintf(line, sizeof(line), "%ld\n",
                              (long)scan.first[confirmed]);
        if (length < 0 || (size_t)length >= sizeof(line)) {
            output_error = EOVERFLOW;
            break;
        }
        if (!completion_write_all(output_fd, line, (size_t)length)) {
            output_error = errno;
            break;
        }
    }
    if (confirmed != 0 &&
        !completion_rewrite_suffix(file, scan.end_offsets[confirmed - 1])) {
        return false;
    }
    *printed = confirmed;
    if (output_error != 0) {
        errno = output_error;
        return false;
    }
    return true;
}
