#ifndef DEMAND_H
#define DEMAND_H

#include "processes.h"

#include <stdbool.h>
#include <stdint.h>

struct demand_startup {
    int directory_fd;
    pid_t pid;
    uint64_t arrival_ns;
    bool initialized;
    bool active;
    bool stopped;
};

/* O diretório pertence ao chamador. Uma sessão só admite um cliente entre
 * criação e consumo; o arquivo existente antes da criação é erro. */
bool demand_init(struct demand_startup *startup, const char *directory);
bool demand_close(struct demand_startup *startup);
bool demand_begin(struct demand_startup *startup,
                  struct processes_watcher *watcher, const char *path,
                  char *const argv[], const char *directory,
                  uint64_t execution_start_ns, pid_t *pid_out,
                  uint64_t *arrival_ns_out);

/* O consumidor único de processos entrega o evento ao adaptador. Uma saída
 * antes da parada libera a sessão e é reportada como erro. */
bool demand_observe(struct demand_startup *startup,
                    const struct processes_event *event);

/* Só consome após SIGSTOP confirmado para o PID criado. Em erro, preserva
 * demanda.txt para diagnóstico e exige demand_abort antes da próxima criação. */
bool demand_consume(struct demand_startup *startup, uint64_t *demand_us);
void demand_abort(struct demand_startup *startup);

#endif
