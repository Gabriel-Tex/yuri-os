#ifndef PROCESSES_H
#define PROCESSES_H

#include <stdbool.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>

enum processes_event_kind
{
    PROCESSES_STOPPED,
    PROCESSES_CONTINUED,
    PROCESSES_EXITED_OK,
    PROCESSES_EXITED_ERROR,
    PROCESSES_SIGNALED
};

struct processes_event
{
    pid_t pid;
    enum processes_event_kind kind;
    int code;
    int raw_status;
};

struct processes_watcher
{
    sigset_t original_mask;
    struct sigaction original_sigchld;
    bool mask_blocked;
    bool action_installed;
    bool initialized;
};

typedef void (*processes_event_handler)(void *context,
                                        const struct processes_event *event);

/* O descritor começa zerado. Bloqueia SIGCHLD no pai e instala um handler
 * que apenas registra a notificação. Uma instância por processo; close
 * restaura até uma inicialização parcial que não pôde ser revertida. */
bool processes_watcher_init(struct processes_watcher *watcher);
bool processes_watcher_close(struct processes_watcher *watcher);

/* Cria o filho sem aguardar exec. O caminho deve ser absoluto; argv[0] existe.
 * Falha de exec/chdir é diagnosticada no filho e aparece como saída 127. */
bool processes_spawn(struct processes_watcher *watcher, const char *path,
                     char *const argv[], const char *working_directory,
                     pid_t *pid_out);

/* Aceita apenas PID positivo para impedir sinalização de grupos. */
bool processes_send(pid_t pid, int signal_number);

/* Recolhe todos os estados disponíveis sem bloquear; o callback é o único
 * consumidor, não bloqueia e recebe cada evento antes da próxima coleta.
 * event_count é opcional e informa a quantidade entregue nesta chamada. */
bool processes_collect(struct processes_watcher *watcher,
                       processes_event_handler handler, void *context,
                       size_t *event_count);

/* Aguarda uma notificação por pselect sem janela entre a verificação e o
 * desbloqueio temporário de SIGCHLD. Chamar após esgotar processes_collect. */
bool processes_wait(struct processes_watcher *watcher);

#endif
