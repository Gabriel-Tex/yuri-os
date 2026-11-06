#define _XOPEN_SOURCE 700
#include "scheduler.h"

#include <errno.h>
#include <stdlib.h>

/* ucontext exige trampoline sem argumentos portáveis. Como há uma única thread
 * do kernel, o despacho registra somente o escalonador ativo durante a troca. */
static struct scheduler *scheduler_active;

/* A fila contém cada rotina READY uma única vez. A remoção arbitrária
 * preserva o despacho manual usado pelos testes de contextos. */
static void scheduler_enqueue(struct scheduler *scheduler, size_t index)
{
    scheduler->ready_queue[scheduler->ready_count++] = index;
}

static void scheduler_remove_ready(struct scheduler *scheduler, size_t index)
{
    size_t position = 0;
    while (scheduler->ready_queue[position] != index)
    {
        position++;
    }
    scheduler->ready_count--;
    for (; position < scheduler->ready_count; position++)
    {
        scheduler->ready_queue[position] = scheduler->ready_queue[position + 1];
    }
}

static void *scheduler_default_allocate(void *context, size_t bytes)
{
    (void)context;
    return malloc(bytes);
}

static void scheduler_default_deallocate(void *context, void *memory)
{
    (void)context;
    free(memory);
}

static void scheduler_trampoline(void)
{
    struct scheduler *scheduler = scheduler_active;
    struct scheduler_thread *thread = &scheduler->threads[scheduler->running_index];
    thread->entry(thread->argument);
    thread->state = SCHEDULER_FINISHED;
    /* uc_link devolve o controle ao dispatcher; esta pilha ainda está ativa. */
}

bool scheduler_init(struct scheduler *scheduler, scheduler_allocate allocate,
                    scheduler_deallocate deallocate, void *allocator_context)
{
    if (scheduler == NULL || (allocate == NULL) != (deallocate == NULL))
    {
        errno = EINVAL;
        return false;
    }
    *scheduler = (struct scheduler){0};
    scheduler->allocate = allocate != NULL ? allocate : scheduler_default_allocate;
    scheduler->deallocate = deallocate != NULL ? deallocate : scheduler_default_deallocate;
    scheduler->allocator_context = allocator_context;
    scheduler->running_index = SCHEDULER_THREAD_COUNT;
    if (getcontext(&scheduler->dispatcher_context) == -1)
    {
        return false;
    }
    scheduler->initialized = true;
    return true;
}

bool scheduler_create(struct scheduler *scheduler, size_t index,
                      scheduler_entry entry, void *argument)
{
    if (scheduler == NULL || !scheduler->initialized ||
        index >= SCHEDULER_THREAD_COUNT || entry == NULL ||
        scheduler->threads[index].state != SCHEDULER_EMPTY ||
        scheduler_active == scheduler)
    {
        errno = EINVAL;
        return false;
    }

    struct scheduler_thread *thread = &scheduler->threads[index];
    void *stack = scheduler->allocate(scheduler->allocator_context,
                                      SCHEDULER_STACK_BYTES);
    if (stack == NULL)
    {
        errno = ENOMEM;
        return false;
    }
    if (getcontext(&thread->context) == -1)
    {
        int saved_errno = errno;
        scheduler->deallocate(scheduler->allocator_context, stack);
        errno = saved_errno;
        return false;
    }
    thread->stack = stack;
    thread->entry = entry;
    thread->argument = argument;
    thread->context.uc_stack.ss_sp = stack;
    thread->context.uc_stack.ss_size = SCHEDULER_STACK_BYTES;
    thread->context.uc_stack.ss_flags = 0;
    thread->context.uc_link = &scheduler->dispatcher_context;
    makecontext(&thread->context, scheduler_trampoline, 0);
    thread->state = SCHEDULER_READY;
    scheduler_enqueue(scheduler, index);
    return true;
}

bool scheduler_resume(struct scheduler *scheduler, size_t index)
{
    if (scheduler == NULL || !scheduler->initialized ||
        index >= SCHEDULER_THREAD_COUNT ||
        scheduler->threads[index].state != SCHEDULER_READY ||
        scheduler_active != NULL)
    {
        errno = EINVAL;
        return false;
    }
    scheduler_remove_ready(scheduler, index);
    scheduler_active = scheduler;
    scheduler->running_index = index;
    scheduler->threads[index].state = SCHEDULER_RUNNING;
    if (swapcontext(&scheduler->dispatcher_context,
                    &scheduler->threads[index].context) == -1)
    {
        int saved_errno = errno;
        scheduler->threads[index].state = SCHEDULER_READY;
        scheduler_enqueue(scheduler, index);
        scheduler->running_index = SCHEDULER_THREAD_COUNT;
        scheduler_active = NULL;
        errno = saved_errno;
        return false;
    }
    scheduler->running_index = SCHEDULER_THREAD_COUNT;
    scheduler_active = NULL;
    return true;
}

bool scheduler_run_events(struct scheduler *scheduler,
                          scheduler_event_callback process,
                          scheduler_event_callback wait, void *context)
{
    if (scheduler == NULL || !scheduler->initialized || scheduler_active != NULL)
    {
        errno = EINVAL;
        return false;
    }
    bool created = false;
    for (size_t i = 0; i < SCHEDULER_THREAD_COUNT; i++)
    {
        created |= scheduler->threads[i].state != SCHEDULER_EMPTY;
    }
    if (!created)
    {
        errno = EINVAL;
        return false;
    }
    for (;;)
    {
        if (process != NULL && !process(context))
        {
            return false;
        }
        if (scheduler->ready_count > 0)
        {
            if (!scheduler_resume(scheduler, scheduler->ready_queue[0]))
            {
                return false;
            }
            continue;
        }
        bool has_waiting = false;
        for (size_t i = 0; i < SCHEDULER_THREAD_COUNT; i++)
        {
            has_waiting |= scheduler->threads[i].state == SCHEDULER_WAITING;
        }
        if (!has_waiting)
        {
            return true;
        }
        if (wait == NULL)
        {
            errno = EAGAIN;
            return false;
        }
        if (!wait(context))
        {
            return false;
        }
    }
}

bool scheduler_run(struct scheduler *scheduler)
{
    return scheduler_run_events(scheduler, NULL, NULL, NULL);
}

bool scheduler_yield(void)
{
    struct scheduler *scheduler = scheduler_active;
    if (scheduler == NULL || scheduler->running_index >= SCHEDULER_THREAD_COUNT)
    {
        errno = EINVAL;
        return false;
    }
    struct scheduler_thread *thread = &scheduler->threads[scheduler->running_index];
    if (thread->state != SCHEDULER_RUNNING)
    {
        errno = EINVAL;
        return false;
    }
    thread->state = SCHEDULER_READY;
    scheduler_enqueue(scheduler, scheduler->running_index);
    if (swapcontext(&thread->context, &scheduler->dispatcher_context) == -1)
    {
        scheduler_remove_ready(scheduler, scheduler->running_index);
        thread->state = SCHEDULER_RUNNING;
        return false;
    }
    return true;
}

bool scheduler_wait(struct scheduler *scheduler, unsigned int event_id,
                    scheduler_condition condition, void *context)
{
    if (scheduler == NULL || scheduler_active != scheduler ||
        scheduler->running_index >= SCHEDULER_THREAD_COUNT ||
        event_id == SCHEDULER_NO_EVENT || condition == NULL)
    {
        errno = EINVAL;
        return false;
    }
    struct scheduler_thread *thread = &scheduler->threads[scheduler->running_index];
    if (thread->state != SCHEDULER_RUNNING)
    {
        errno = EINVAL;
        return false;
    }
    while (!condition(context))
    {
        /* Um sinal pode alterar uma condição entre a primeira leitura e a
         * suspensão; neste caso a rotina continua sem perder o evento. */
        if (condition(context))
        {
            break;
        }
        thread->wait_event = event_id;
        thread->state = SCHEDULER_WAITING;
        if (swapcontext(&thread->context, &scheduler->dispatcher_context) == -1)
        {
            int saved_errno = errno;
            thread->wait_event = SCHEDULER_NO_EVENT;
            thread->state = SCHEDULER_RUNNING;
            errno = saved_errno;
            return false;
        }
    }
    return true;
}

int scheduler_notify(struct scheduler *scheduler, unsigned int event_id)
{
    if (scheduler == NULL || !scheduler->initialized ||
        event_id == SCHEDULER_NO_EVENT ||
        (scheduler_active != NULL && scheduler_active != scheduler))
    {
        errno = EINVAL;
        return -1;
    }
    int woken = 0;
    for (size_t i = 0; i < SCHEDULER_THREAD_COUNT; i++)
    {
        struct scheduler_thread *thread = &scheduler->threads[i];
        if (thread->state == SCHEDULER_WAITING && thread->wait_event == event_id)
        {
            thread->wait_event = SCHEDULER_NO_EVENT;
            thread->state = SCHEDULER_READY;
            scheduler_enqueue(scheduler, i);
            woken++;
        }
    }
    return woken;
}

enum scheduler_thread_state scheduler_state(const struct scheduler *scheduler,
                                            size_t index)
{
    if (scheduler == NULL || !scheduler->initialized || index >= SCHEDULER_THREAD_COUNT)
    {
        return SCHEDULER_EMPTY;
    }
    return scheduler->threads[index].state;
}

bool scheduler_destroy(struct scheduler *scheduler)
{
    if (scheduler == NULL || scheduler_active == scheduler)
    {
        errno = EINVAL;
        return false;
    }
    if (!scheduler->initialized)
    {
        return true;
    }
    for (size_t i = 0; i < SCHEDULER_THREAD_COUNT; i++)
    {
        if (scheduler->threads[i].stack != NULL)
        {
            scheduler->deallocate(scheduler->allocator_context,
                                  scheduler->threads[i].stack);
            scheduler->threads[i].stack = NULL;
        }
        scheduler->threads[i].state = SCHEDULER_EMPTY;
        scheduler->threads[i].wait_event = SCHEDULER_NO_EVENT;
    }
    scheduler->ready_count = 0;
    scheduler->running_index = SCHEDULER_THREAD_COUNT;
    scheduler->initialized = false;
    return true;
}
