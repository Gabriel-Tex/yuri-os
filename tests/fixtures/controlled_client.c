#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
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
    /* O modo por ambiente permite injetar falhas na CLI sem alterar o
     * contrato de argumentos do cliente usado pelo serviço. */
    const char *mode = getenv("CONTROLLED_CLIENT_MODE");
    char *mode_arguments[3];
    if (argc == 1 && mode != NULL && mode[0] != '\0')
    {
        mode_arguments[0] = argv[0];
        mode_arguments[1] = (char *)mode;
        mode_arguments[2] = NULL;
        argv = mode_arguments;
        argc = 2;
    }
    if (argc == 1 || (argc == 2 &&
        (strcmp(argv[1], "service_late") == 0 ||
         strcmp(argv[1], "service_signal") == 0 ||
         strcmp(argv[1], "service_hold") == 0)))
    {
        FILE *file = fopen("demanda.txt", "w");
        if (file == NULL) return 20;
        int written = fputs("1000\n", file);
        int closed = fclose(file);
        if (written == EOF || closed == EOF) return 20;
        if (raise(SIGSTOP) != 0) return 21;
        if (argc == 2 && strcmp(argv[1], "service_signal") == 0)
        {
            raise(SIGTERM);
            return 22;
        }
        if (argc == 2)
        {
            struct timespec pause = {.tv_sec = 0, .tv_nsec = 30000000};
            while (nanosleep(&pause, &pause) == -1 && errno == EINTR) {}
        }
        sem_t *semaphore = sem_open("/sem_atend", 0);
        if (semaphore == SEM_FAILED) return 23;
        int acquired;
        do { acquired = sem_wait(semaphore); }
        while (acquired == -1 && errno == EINTR);
        if (acquired == -1) { sem_close(semaphore); return 24; }
        if (argc == 2 && strcmp(argv[1], "service_hold") == 0)
        {
            sem_close(semaphore);
            return 0;
        }
        struct timespec service = {.tv_sec = 0, .tv_nsec = 1000000};
        while (nanosleep(&service, &service) == -1 && errno == EINTR) {}
        int released = sem_post(semaphore);
        int closed_semaphore = sem_close(semaphore);
        return released == 0 && closed_semaphore == 0 ? 0 : 25;
    }
    if (argc < 2)
    {
        return 12;
    }
    if (strcmp(argv[1], "exit0") == 0)
    {
        return 0;
    }
    if (strcmp(argv[1], "demand") == 0 && argc == 3)
    {
        FILE *file = fopen("demanda.txt", "w");
        if (file == NULL || fputs(argv[2], file) == EOF || fclose(file) == EOF)
        {
            return 20;
        }
        if (raise(SIGSTOP) != 0)
        {
            return 21;
        }
        return 0;
    }
    if (strcmp(argv[1], "demand_delayed") == 0 && argc == 3)
    {
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 100000000};
        while (nanosleep(&pause, &pause) == -1 && errno == EINTR)
        {
        }
        FILE *file = fopen("demanda.txt", "w");
        if (file == NULL || fputs(argv[2], file) == EOF || fclose(file) == EOF)
        {
            return 20;
        }
        return raise(SIGSTOP) == 0 ? 0 : 21;
    }
    if (strcmp(argv[1], "demand_missing") == 0)
    {
        return raise(SIGSTOP) == 0 ? 0 : 21;
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
