#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int check_descriptor(const char *number)
{
    char *end;
    errno = 0;
    long descriptor = strtol(number, &end, 10);
    if (errno != 0 || *end != '\0' || descriptor < 3 || descriptor > 1000000)
    {
        return 10;
    }
    errno = 0;
    return fcntl((int)descriptor, F_GETFD) == -1 && errno == EBADF ? 0 : 11;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        return 12;
    }
    if (strcmp(argv[1], "exit0") == 0)
    {
        return 0;
    }
    if (strcmp(argv[1], "exit7") == 0)
    {
        return 7;
    }
    if (strcmp(argv[1], "signal") == 0)
    {
        raise(SIGTERM);
        return 13;
    }
    if (strcmp(argv[1], "stop") == 0)
    {
        sigset_t release_signal;
        if (sigemptyset(&release_signal) == -1 ||
            sigaddset(&release_signal, SIGUSR1) == -1 ||
            sigprocmask(SIG_BLOCK, &release_signal, NULL) == -1)
        {
            return 14;
        }
        if (raise(SIGSTOP) != 0)
        {
            return 15;
        }
        int received;
        if (sigwait(&release_signal, &received) != 0 || received != SIGUSR1)
        {
            return 16;
        }
        return 0;
    }
    if (strcmp(argv[1], "mask") == 0)
    {
        sigset_t mask;
        if (sigprocmask(SIG_BLOCK, NULL, &mask) == -1)
        {
            return 17;
        }
        return sigismember(&mask, SIGCHLD) == 0 &&
                       sigismember(&mask, SIGUSR2) == 1
                   ? 0
                   : 18;
    }
    if (strcmp(argv[1], "fd") == 0 && argc == 3)
    {
        return check_descriptor(argv[2]);
    }
    if (strcmp(argv[1], "cwd") == 0 && argc == 3)
    {
        char *directory = getcwd(NULL, 0);
        if (directory == NULL)
        {
            return 17;
        }
        int result = strcmp(directory, argv[2]) == 0 ? 0 : 18;
        free(directory);
        return result;
    }
    return 19;
}
