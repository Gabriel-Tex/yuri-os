#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define CLIENT_NS_PER_MS UINT64_C(1000000)
#define CLIENT_MAX_PATIENCE_MS (UINT64_MAX / CLIENT_NS_PER_MS)

enum client_priority
{
    CLIENT_PRIORITY_HIGH,
    CLIENT_PRIORITY_LOW
};

enum client_state
{
    CLIENT_STARTING,
    CLIENT_QUEUED,
    CLIENT_IN_SERVICE,
    CLIENT_COMPLETED,
    CLIENT_CANCELLED,
    CLIENT_FAILED
};

/* Instantes relativos ao início da execução; demanda mantém a unidade do cliente.
 * A chegada é fornecida pela aplicação na criação e não muda nas transições.
 * Demanda só é válida após enfileirar; conclusão só em COMPLETED. */
struct client_record
{
    pid_t pid;
    uint64_t sequence;
    enum client_priority priority;
    uint64_t arrival_ns;
    uint64_t patience_ns;
    uint64_t deadline_ns;
    bool deadline_carry;
    uint64_t demand_us;
    enum client_state state;
    uint64_t completion_ns;
};

/* Inicializa armazenamento sem proprietário ativo. PID e X devem ser positivos;
 * sequência e instantes são fornecidos pelo chamador. O prazo soma chegada e
 * paciência com um bit de transporte; X acima do limite é recusado. */
bool client_init(struct client_record *client, pid_t pid, uint64_t sequence,
                 enum client_priority priority, uint64_t arrival_ns,
                 uint64_t max_patience_ms);

/* STARTING -> QUEUED transfere Recepção -> fila após ler a demanda completa;
 * QUEUED -> IN_SERVICE transfere fila -> Atendente. Demanda zero é representável.
 * As funções só alteram dados: o chamador coordena processos e propriedade. */
bool client_enqueue(struct client_record *client, uint64_t demand_us);
bool client_start(struct client_record *client);

/* COMPLETED exige IN_SERVICE e confirmação externa do término normal; CANCELLED
 * aceita STARTING/QUEUED; FAILED aceita qualquer estado não terminal.
 * Estados terminais não têm saída. Liberação exige também recolher o filho. */
bool client_complete(struct client_record *client, uint64_t completion_ns);
bool client_cancel(struct client_record *client);
bool client_fail(struct client_record *client);

#endif
