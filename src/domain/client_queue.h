#ifndef CLIENT_QUEUE_H
#define CLIENT_QUEUE_H

#include "client.h"

#include <stdbool.h>
#include <stddef.h>

#define CLIENT_QUEUE_CAPACITY 100

/* A reserva corresponde ao único cliente STARTING, mantido pela Recepção fora
 * do vetor. Os registros do vetor pertencem à fila até serem retirados. */
struct client_queue
{
    struct client_record records[CLIENT_QUEUE_CAPACITY];
    size_t count;
    bool reserved;
};

/* Inicializar apenas antes do uso ou depois que todos os registros saírem. */
bool client_queue_init(struct client_queue *queue);

/* Retorna count + 1 quando há uma reserva. */
size_t client_queue_waiting_count(const struct client_queue *queue);

/* Retorna true se houver espaço para mais clientes na fila. */
bool client_queue_has_space(const struct client_queue *queue);

/* Reserva um slot na fila para um novo cliente. */
bool client_queue_reserve(struct client_queue *queue);

/* Libera a reserva de um slot na fila. */
bool client_queue_release_reservation(struct client_queue *queue);

/* A inserção exige uma reserva e um registro QUEUED completo. O chamador deixa
 * de possuir o registro ativo após o sucesso. Falhas preservam fila e reserva. */
bool client_queue_insert(struct client_queue *queue,
                         const struct client_record *client);

/* Retira o menor prazo absoluto; empates usam a sequência de criação. O
 * destino deve ficar fora do armazenamento da fila. Vazio não altera destino. */
bool client_queue_take_next(struct client_queue *queue,
                            struct client_record *client);

#endif
