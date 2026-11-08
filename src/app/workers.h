#ifndef WORKERS_H
#define WORKERS_H

#include "service.h"
#include "domain/client_queue.h"
#include "domain/metrics.h"
#include "platform/demand.h"

#include <stdbool.h>
#include <stdint.h>

#define WORKERS_SPACE_EVENT 2u
#define WORKERS_START_EVENT 3u
#define WORKERS_CLIENT_EVENT 4u
#define WORKERS_TERMINAL_EVENT 5u

typedef void (*workers_other_event_handler)(void *context,
                                             const struct processes_event *event);

struct workers_reception {
    struct service_state *service;
    struct client_queue *queue;
    struct metrics *metrics;
    const char *client_path;
    char *const *client_argv;
    bool *stop_requested;
    struct demand_startup demand;
    struct client_record starting;
    workers_other_event_handler other_handler;
    void *other_context;
    uint64_t random_state;
    uint64_t next_sequence;
    size_t maximum_waiting;
    bool starting_active;
    bool starting_record_valid;
    bool starting_reaped;
    bool finished;
    int error;
};

/* A fonte de sorteio é iniciada uma vez. Cada bit tem mapeamento 1:1. */
enum client_priority workers_priority_from_bits(uint64_t bits);
bool workers_reception_init(struct workers_reception *reception,
                            struct service_state *service,
                            struct client_queue *queue, struct metrics *metrics,
                            const char *client_path, char *const client_argv[],
                            bool *stop_requested);
void workers_reception_run(void *argument);
void workers_reception_on_event(void *context,
                                const struct processes_event *event);
bool workers_reception_close(struct workers_reception *reception);

#endif
