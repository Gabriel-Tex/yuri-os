#define _POSIX_C_SOURCE 200809L
#include "interrupt.h"

#include <errno.h>
#include <stddef.h>

static struct interrupt_watcher *interrupt_active;
static volatile sig_atomic_t interrupt_flag;

static void interrupt_on_signal(int signal_number)
{
    int saved_errno = errno;
    (void)signal_number;
    interrupt_flag = 1;
    errno = saved_errno;
}

bool interrupt_init(struct interrupt_watcher *watcher)
{
    if (watcher == NULL || interrupt_active != NULL || watcher->mask_saved ||
        watcher->int_saved || watcher->term_saved) {
        errno = EINVAL;
        return false;
    }
    sigset_t blocked;
    if (sigemptyset(&blocked) == -1 || sigaddset(&blocked, SIGINT) == -1 ||
        sigaddset(&blocked, SIGTERM) == -1 ||
        sigprocmask(SIG_BLOCK, &blocked, &watcher->original_mask) == -1)
        return false;
    watcher->mask_saved = true;
    interrupt_active = watcher;
    interrupt_flag = 0;
    struct sigaction action = {.sa_handler = interrupt_on_signal};
    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(SIGINT, &action, &watcher->original_int) == -1)
        goto fail;
    watcher->int_saved = true;
    if (sigaction(SIGTERM, &action, &watcher->original_term) == -1)
        goto fail;
    watcher->term_saved = true;
    return true;
fail: {
        int error = errno;
        interrupt_close(watcher);
        errno = error;
        return false;
    }
}

bool interrupt_requested(const struct interrupt_watcher *watcher)
{
    return watcher != NULL && interrupt_active == watcher &&
           interrupt_flag != 0;
}

bool interrupt_close(struct interrupt_watcher *watcher)
{
    if (watcher == NULL ||
        ((watcher->mask_saved || watcher->int_saved || watcher->term_saved) &&
         interrupt_active != watcher)) {
        errno = EINVAL;
        return false;
    }
    int error = 0;
    /* Sinais pendentes da limpeza são observados pelo handler antes de
     * restaurar a disposição anterior. */
    if (watcher->mask_saved) {
        if (sigprocmask(SIG_SETMASK, &watcher->original_mask, NULL) == -1) {
            if (error == 0) error = errno;
        } else {
            watcher->mask_saved = false;
        }
    }
    if (watcher->term_saved) {
        if (sigaction(SIGTERM, &watcher->original_term, NULL) == -1)
            error = errno;
        else watcher->term_saved = false;
    }
    if (watcher->int_saved) {
        if (sigaction(SIGINT, &watcher->original_int, NULL) == -1) {
            if (error == 0) error = errno;
        } else {
            watcher->int_saved = false;
        }
    }
    if (error != 0) {
        errno = error;
        return false;
    }
    interrupt_active = NULL;
    interrupt_flag = 0;
    return true;
}
