#ifndef INTERRUPT_H
#define INTERRUPT_H

#include <stdbool.h>
#include <signal.h>

struct interrupt_watcher {
    sigset_t original_mask;
    struct sigaction original_int;
    struct sigaction original_term;
    bool mask_saved;
    bool int_saved;
    bool term_saved;
};

/* Bloqueia SIGINT/SIGTERM fora da espera atômica e instala handlers que
 * apenas registram a interrupção. Uma instância ativa por processo. */
bool interrupt_init(struct interrupt_watcher *watcher);
bool interrupt_requested(const struct interrupt_watcher *watcher);
bool interrupt_close(struct interrupt_watcher *watcher);

#endif
