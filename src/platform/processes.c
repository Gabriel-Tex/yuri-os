#define _GNU_SOURCE
#include "processes.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

static struct processes_watcher *processes_active;
static volatile sig_atomic_t processes_notified;

static void processes_on_sigchld(int signal_number)
{
    int saved_errno = errno;
    (void)signal_number;
    processes_notified = 1;
    errno = saved_errno;
}

static void processes_child_write(const char *message, size_t length)
{
    while (length > 0)
    {
        ssize_t written = write(STDERR_FILENO, message, length);
        if (written == -1 && errno == EINTR)
        {
            continue;
        }
        if (written <= 0)
        {
            break;
        }
        message += written;
        length -= (size_t)written;
    }
}

static void processes_child_fail(const char *message, size_t length,
                                 int error_number)
{
    processes_child_write(message, length);
    static const char prefix[] = " (errno ";
    processes_child_write(prefix, sizeof(prefix) - 1);
    char digits[3 * sizeof(int) + 1];
    size_t digit_count = 0;
    unsigned int number = (unsigned int)error_number;
    do
    {
        digits[digit_count++] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);
    while (digit_count > 0)
    {
        digit_count--;
        processes_child_write(&digits[digit_count], 1);
    }
    static const char suffix[] = ")\n";
    processes_child_write(suffix, sizeof(suffix) - 1);
    _exit(127);
}

/* close_range evita percorrer todos os descritores a cada cliente. Em um
 * kernel antigo, o limite de descritores do pai delimita o fallback. */
static bool processes_close_child_descriptors(rlim_t open_limit)
{
    if (close_range(3, ~0u, 0) == 0)
    {
        return true;
    }
    if (errno != ENOSYS && errno != EINVAL)
    {
        return false;
    }
    if (open_limit == RLIM_INFINITY || open_limit > INT_MAX)
    {
        errno = EOVERFLOW;
        return false;
    }
    for (int descriptor = 3; descriptor < (int)open_limit; descriptor++)
    {
        close(descriptor);
    }
    return true;
}

bool processes_watcher_init(struct processes_watcher *watcher)
{
    if (watcher == NULL || processes_active != NULL)
    {
        errno = EINVAL;
        return false;
    }
    *watcher = (struct processes_watcher){0};
    sigset_t block_mask;
    if (sigemptyset(&block_mask) == -1 || sigaddset(&block_mask, SIGCHLD) == -1 ||
        sigprocmask(SIG_BLOCK, &block_mask, &watcher->original_mask) == -1)
    {
        return false;
    }
    watcher->mask_blocked = true;
    processes_active = watcher;
    struct sigaction action = {0};
    action.sa_handler = processes_on_sigchld;
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGCHLD, &action, &watcher->original_sigchld) == -1)
    {
        int saved_errno = errno;
        if (processes_watcher_close(watcher))
        {
            errno = saved_errno;
        }
        return false;
    }
    watcher->action_installed = true;
    processes_notified = 0;
    watcher->initialized = true;
    return true;
}

bool processes_watcher_close(struct processes_watcher *watcher)
{
    if (watcher == NULL ||
        ((watcher->mask_blocked || watcher->action_installed ||
          watcher->initialized) &&
         processes_active != watcher))
    {
        errno = EINVAL;
        return false;
    }
    if (!watcher->mask_blocked && !watcher->action_installed)
    {
        return true;
    }
    watcher->initialized = false;
    if (watcher->action_installed)
    {
        if (sigaction(SIGCHLD, &watcher->original_sigchld, NULL) == -1)
        {
            return false;
        }
        watcher->action_installed = false;
    }
    if (watcher->mask_blocked)
    {
        if (sigprocmask(SIG_SETMASK, &watcher->original_mask, NULL) == -1)
        {
            return false;
        }
        watcher->mask_blocked = false;
    }
    processes_active = NULL;
    processes_notified = 0;
    watcher->initialized = false;
    return true;
}

bool processes_spawn(struct processes_watcher *watcher, const char *path,
                     char *const argv[], const char *working_directory,
                     pid_t *pid_out)
{
    if (watcher == NULL || !watcher->initialized || processes_active != watcher ||
        path == NULL || path[0] != '/' || argv == NULL || argv[0] == NULL ||
        pid_out == NULL ||
        (working_directory != NULL && working_directory[0] == '\0'))
    {
        errno = EINVAL;
        return false;
    }
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) == -1)
    {
        return false;
    }
    pid_t pid;
    do
    {
        pid = fork();
    } while (pid == -1 && errno == EINTR);
    if (pid == -1)
    {
        return false;
    }
    if (pid == 0)
    {
        if (sigaction(SIGCHLD, &watcher->original_sigchld, NULL) == -1 ||
            sigprocmask(SIG_SETMASK, &watcher->original_mask, NULL) == -1)
        {
            static const char message[] = "processes: falha ao restaurar sinais";
            processes_child_fail(message, sizeof(message) - 1, errno);
        }
        if (working_directory != NULL && chdir(working_directory) == -1)
        {
            static const char message[] = "processes: falha ao mudar diretório";
            processes_child_fail(message, sizeof(message) - 1, errno);
        }
        if (!processes_close_child_descriptors(limit.rlim_cur))
        {
            static const char message[] = "processes: falha ao fechar descritores";
            processes_child_fail(message, sizeof(message) - 1, errno);
        }
        execv(path, argv);
        static const char message[] = "processes: falha ao executar processo";
        processes_child_fail(message, sizeof(message) - 1, errno);
    }
    *pid_out = pid;
    return true;
}

bool processes_send(pid_t pid, int signal_number)
{
    if (pid <= 0 || signal_number <= 0)
    {
        errno = EINVAL;
        return false;
    }
    int result;
    do
    {
        result = kill(pid, signal_number);
    } while (result == -1 && errno == EINTR);
    return result == 0;
}

bool processes_collect(struct processes_watcher *watcher,
                       processes_event_handler handler, void *context,
                       size_t *event_count)
{
    if (watcher == NULL || !watcher->initialized || processes_active != watcher ||
        handler == NULL)
    {
        errno = EINVAL;
        return false;
    }
    processes_notified = 0;
    size_t collected = 0;
    for (;;)
    {
        int raw_status;
        pid_t pid = waitpid(-1, &raw_status, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid == 0 || (pid == -1 && errno == ECHILD))
        {
            if (event_count != NULL)
            {
                *event_count = collected;
            }
            return true;
        }
        if (pid == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        struct processes_event event = {.pid = pid, .raw_status = raw_status};
        if (WIFSTOPPED(raw_status))
        {
            event.kind = PROCESSES_STOPPED;
            event.code = WSTOPSIG(raw_status);
        }
        else if (WIFCONTINUED(raw_status))
        {
            event.kind = PROCESSES_CONTINUED;
            event.code = SIGCONT;
        }
        else if (WIFEXITED(raw_status))
        {
            event.code = WEXITSTATUS(raw_status);
            event.kind = event.code == 0 ? PROCESSES_EXITED_OK : PROCESSES_EXITED_ERROR;
        }
        else if (WIFSIGNALED(raw_status))
        {
            event.kind = PROCESSES_SIGNALED;
            event.code = WTERMSIG(raw_status);
        }
        else
        {
            errno = EPROTO;
            return false;
        }
        handler(context, &event);
        collected++;
    }
}

bool processes_wait(struct processes_watcher *watcher)
{
    return processes_wait_input(watcher, -1);
}

bool processes_wait_input(struct processes_watcher *watcher, int descriptor)
{
    if (watcher == NULL || !watcher->initialized || processes_active != watcher ||
        descriptor >= FD_SETSIZE)
    {
        errno = EINVAL;
        return false;
    }
    if (processes_notified)
    {
        return true;
    }
    sigset_t wait_mask = watcher->original_mask;
    if (sigdelset(&wait_mask, SIGCHLD) == -1)
    {
        return false;
    }
    fd_set readers;
    fd_set *readers_ptr = NULL;
    int width = 0;
    if (descriptor >= 0) {
        FD_ZERO(&readers);
        FD_SET(descriptor, &readers);
        readers_ptr = &readers;
        width = descriptor + 1;
    }
    int result = pselect(width, readers_ptr, NULL, NULL, NULL, &wait_mask);
    return result >= 0 || (result == -1 && errno == EINTR);
}
