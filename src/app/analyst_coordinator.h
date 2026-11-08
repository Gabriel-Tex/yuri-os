#ifndef ANALYST_COORDINATOR_H
#define ANALYST_COORDINATOR_H

#include "platform/completion_file.h"
#include "platform/processes.h"
#include "runtime/scheduler.h"

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define ANALYST_COORDINATOR_EVENT 1u

enum analyst_coordinator_state {
    ANALYST_COORDINATOR_STARTING,
    ANALYST_COORDINATOR_STOPPED,
    ANALYST_COORDINATOR_ACTIVE,
    ANALYST_COORDINATOR_DEAD
};

struct analyst_coordinator {
    pid_t pid;
    struct completion_file *file;
    struct scheduler *scheduler;
    enum analyst_coordinator_state state;
    size_t pending;
    size_t expected_printed;
    bool draining;
    bool initialized;
};

/* O serviço cria o filho e é o consumidor único dos eventos de processos. */
bool analyst_coordinator_init(struct analyst_coordinator *coordinator, pid_t pid,
                              struct completion_file *file,
                              struct scheduler *scheduler);
bool analyst_coordinator_observe(struct analyst_coordinator *coordinator,
                                 const struct processes_event *event);

/* Atendente tenta incluir sem bloquear. Se o Analista está ativo, mantém o PID
 * consigo e espera ANALYST_COORDINATOR_EVENT no escalonador. */
bool analyst_coordinator_can_append(void *context);
bool analyst_coordinator_try_append(struct analyst_coordinator *coordinator,
                                    pid_t pid, bool *appended);

/* Política D12: dez pendências, ociosidade ou drenagem. SIGCONT só após parada
 * confirmada; a próxima parada valida o consumo do lote. */
bool analyst_coordinator_maybe_wake(struct analyst_coordinator *coordinator,
                                    bool idle, bool *woken);
bool analyst_coordinator_request_drain(struct analyst_coordinator *coordinator);
bool analyst_coordinator_is_drained(struct analyst_coordinator *coordinator,
                                    bool *drained);

#endif
