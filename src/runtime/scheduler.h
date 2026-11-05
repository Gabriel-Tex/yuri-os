#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <ucontext.h>

#define SCHEDULER_THREAD_COUNT 2
#define SCHEDULER_STACK_BYTES (64u * 1024u)

enum scheduler_thread_state
{
    SCHEDULER_EMPTY,
    SCHEDULER_READY,
    SCHEDULER_RUNNING,
    SCHEDULER_FINISHED
};

typedef void (*scheduler_entry)(void *argument);
typedef void *(*scheduler_allocate)(void *context, size_t bytes);
typedef void (*scheduler_deallocate)(void *context, void *memory);

struct scheduler_thread
{
    ucontext_t context;
    void *stack;
    scheduler_entry entry;
    void *argument;
    enum scheduler_thread_state state;
};

struct scheduler
{
    ucontext_t dispatcher_context;
    struct scheduler_thread threads[SCHEDULER_THREAD_COUNT];
    scheduler_allocate allocate;
    scheduler_deallocate deallocate;
    void *allocator_context;
    size_t running_index;
    bool initialized;
};

/* O chamador possui o descritor; o runtime possui as pilhas. Alocador e
 * liberador devem ser fornecidos juntos ou ambos nulos (malloc/free). */
bool scheduler_init(struct scheduler *scheduler, scheduler_allocate allocate,
                    scheduler_deallocate deallocate, void *allocator_context);

/* Cria uma nova thread no escalonador. */
bool scheduler_create(struct scheduler *scheduler, size_t index,
                      scheduler_entry entry, void *argument);

/* US04 expõe despacho manual para testar contextos. US05 definirá a política.
 * yield só pode ser chamado pela rotina em execução; término volta via uc_link. */
bool scheduler_resume(struct scheduler *scheduler, size_t index);

/* Devolve o controle para o escalonador. */
bool scheduler_yield(void);

/* Retorna o estado da thread no escalonador. */
enum scheduler_thread_state scheduler_state(const struct scheduler *scheduler,
                                            size_t index);

/* Apenas a partir do contexto do escalonador, após retorno ou cessão. Aceita
 * inicialização parcial e pode ser repetida após liberar as pilhas. */
bool scheduler_destroy(struct scheduler *scheduler);

#endif
