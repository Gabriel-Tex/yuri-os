#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <ucontext.h>

#define SCHEDULER_THREAD_COUNT 2
#define SCHEDULER_STACK_BYTES (64u * 1024u)
#define SCHEDULER_NO_EVENT 0u

enum scheduler_thread_state
{
    SCHEDULER_EMPTY,
    SCHEDULER_READY,
    SCHEDULER_RUNNING,
    SCHEDULER_WAITING,
    SCHEDULER_FINISHED
};

typedef void (*scheduler_entry)(void *argument);
typedef void *(*scheduler_allocate)(void *context, size_t bytes);
typedef void (*scheduler_deallocate)(void *context, void *memory);
typedef bool (*scheduler_condition)(void *context);
typedef bool (*scheduler_event_callback)(void *context);

struct scheduler_thread
{
    ucontext_t context;
    void *stack;
    scheduler_entry entry;
    void *argument;
    enum scheduler_thread_state state;
    unsigned int wait_event;
};

struct scheduler
{
    ucontext_t dispatcher_context;
    struct scheduler_thread threads[SCHEDULER_THREAD_COUNT];
    scheduler_allocate allocate;
    scheduler_deallocate deallocate;
    void *allocator_context;
    size_t running_index;
    size_t ready_queue[SCHEDULER_THREAD_COUNT];
    size_t ready_count;
    bool initialized;
};

/* O chamador possui o descritor; o runtime possui as pilhas. Alocador e
 * liberador devem ser fornecidos juntos ou ambos nulos (malloc/free). */
bool scheduler_init(struct scheduler *scheduler, scheduler_allocate allocate,
                    scheduler_deallocate deallocate, void *allocator_context);

/* Cria uma nova thread no escalonador. */
bool scheduler_create(struct scheduler *scheduler, size_t index,
                      scheduler_entry entry, void *argument);

/* Despacha uma rotina pronta explicitamente; a fila mantém as demais prontas. */
bool scheduler_resume(struct scheduler *scheduler, size_t index);

/* Despacha as prontas em round-robin até todas as criadas terminarem.
 * Sem função de espera, retorna EAGAIN se todas as ativas aguardam eventos. */
bool scheduler_run(struct scheduler *scheduler);

/* Processa eventos antes de cada despacho. Sem prontas, espera por um evento
 * e volta a processá-los. Callbacks retornam false e definem errno em falha;
 * process não bloqueia, wait bloqueia até um evento observável. */
bool scheduler_run_events(struct scheduler *scheduler,
                          scheduler_event_callback process,
                          scheduler_event_callback wait, void *context);

/* Devolve o controle ao escalonador e recoloca a rotina no fim da fila. */
bool scheduler_yield(void);

/* A condição, sem bloqueio ou efeitos colaterais, é verificada antes de
 * suspender e depois de cada notificação. O evento positivo identifica a
 * condição; todos os interessados a reavaliam antes de prosseguir. */
bool scheduler_wait(struct scheduler *scheduler, unsigned int event_id,
                    scheduler_condition condition, void *context);

/* Acorda as rotinas que aguardam event_id; retorna a quantidade acordada.
 * Evento zero é inválido e retorna -1 com EINVAL. */
int scheduler_notify(struct scheduler *scheduler, unsigned int event_id);

/* Retorna o estado da thread no escalonador. */
enum scheduler_thread_state scheduler_state(const struct scheduler *scheduler,
                                            size_t index);

/* Apenas a partir do contexto do escalonador, após retorno ou cessão. Aceita
 * inicialização parcial e pode ser repetida após liberar as pilhas. */
bool scheduler_destroy(struct scheduler *scheduler);

#endif
