#define _XOPEN_SOURCE 700
#include "scheduler.h"

#include <errno.h>
#include <stdlib.h>

/* ucontext exige trampoline sem argumentos portáveis. Como há uma única thread
 * do kernel, o despacho registra somente o escalonador ativo durante a troca. */
static struct scheduler *scheduler_active;

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
    scheduler_active = scheduler;
    scheduler->running_index = index;
    scheduler->threads[index].state = SCHEDULER_RUNNING;
    if (swapcontext(&scheduler->dispatcher_context,
                    &scheduler->threads[index].context) == -1)
    {
        int saved_errno = errno;
        scheduler->threads[index].state = SCHEDULER_READY;
        scheduler->running_index = SCHEDULER_THREAD_COUNT;
        scheduler_active = NULL;
        errno = saved_errno;
        return false;
    }
    scheduler->running_index = SCHEDULER_THREAD_COUNT;
    scheduler_active = NULL;
    return true;
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
    if (swapcontext(&thread->context, &scheduler->dispatcher_context) == -1)
    {
        thread->state = SCHEDULER_RUNNING;
        return false;
    }
    return true;
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
    }
    scheduler->initialized = false;
    return true;
}
