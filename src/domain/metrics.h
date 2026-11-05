#ifndef METRICS_H
#define METRICS_H

#include "client.h"

#include <stdbool.h>
#include <stdint.h>

struct metrics
{
    uint64_t created;
    uint64_t completed;
    uint64_t satisfied;
    uint64_t cancelled;
    uint64_t failed;
};

/* Inicializa a estrutura de métricas. */
bool metrics_init(struct metrics *metrics);

/* Chamar uma vez por fork bem-sucedido; eventos terminais exigem um cliente
 * criado ainda sem evento terminal. Recusas não alteram contadores ou registro. */
bool metrics_record_created(struct metrics *metrics);
bool metrics_record_completed(struct metrics *metrics, struct client_record *client,
                              uint64_t completion_ns);
bool metrics_record_cancelled(struct metrics *metrics, struct client_record *client);
bool metrics_record_failed(struct metrics *metrics, struct client_record *client);

/* Retornam false sem alterar rate quando a taxa não se aplica. O finito só
 * apresenta sucesso com criados = concluídos = N e sem perdas. */
bool metrics_finite_rate(const struct metrics *metrics, uint64_t total_clients,
                         double *rate);
bool metrics_infinite_rate(const struct metrics *metrics, double *rate);

#endif
