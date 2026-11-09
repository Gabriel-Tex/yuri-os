#ifndef SERVICE_H
#define SERVICE_H

#include "analyst_coordinator.h"
#include "platform/clock.h"
#include "platform/completion_file.h"
#include "platform/ipc.h"
#include "platform/processes.h"
#include "platform/terminal.h"
#include "runtime/scheduler.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

struct service_config {
    uint64_t client_count;
    uint64_t max_patience_ms;
    const char *project_root;
    const char *analyst_path;
};

struct service_result {
    uint64_t created;
    uint64_t completed;
    uint64_t satisfied;
    uint64_t cancelled;
    uint64_t failed;
    uint64_t elapsed_ns;
    bool prepared;
    bool complete;
};

typedef void (*service_client_event_handler)(void *context,
                                              const struct processes_event *event);

struct service_state {
    struct service_config config;
    struct ipc_service ipc;
    struct processes_watcher watcher;
    struct completion_file completion;
    struct scheduler scheduler;
    struct analyst_coordinator coordinator;
    struct terminal_input input;
    char *run_directory;
    pid_t analyst_pid;
    uint64_t start_ns;
    bool analyst_reaped;
    bool initialized;
    bool input_enabled;
    bool stop_requested;
    int event_error;
    service_client_event_handler client_handler;
    void *client_context;
};

/* Inicia recursos sem prometer atendimento. O chamador fornece caminhos
 * absolutos e cria as duas rotinas no scheduler antes de executá-lo. */
bool service_prepare(struct service_state *service,
                     const struct service_config *config);
bool service_process_events(void *context);
bool service_wait_events(void *context);
/* Executa N > 0 com o cliente indicado; exige todos os atendimentos e drena
 * o arquivo antes de devolver sucesso. A liberação final é service_close. */
bool service_run_finite(struct service_state *service, const char *client_path,
                        struct service_result *result);
bool service_run_infinite(struct service_state *service, const char *client_path,
                          struct service_result *result);
bool service_close(struct service_state *service, struct service_result *result);

#endif
