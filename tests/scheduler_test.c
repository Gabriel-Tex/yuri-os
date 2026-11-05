#include "runtime/scheduler.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

struct trace {
    int values[8];
    size_t count;
};

struct worker_argument {
    struct trace *trace;
    int id;
    int yields;
};

static void worker(void *argument) {
    struct worker_argument *worker_argument = argument;
    int local = worker_argument->id * 10;
    for (int step = 0; step < worker_argument->yields; step++) {
        worker_argument->trace->values[worker_argument->trace->count++] = local;
        CHECK(scheduler_yield());
        local += worker_argument->id;
    }
    worker_argument->trace->values[worker_argument->trace->count++] = local;
}

static void check_trace(const struct trace *trace, const int *expected,
                        size_t count) {
    CHECK(trace->count == count);
    for (size_t i = 0; i < count; i++) {
        CHECK(trace->values[i] == expected[i]);
    }
}

static void test_contexts(bool first_finishes_first) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    struct worker_argument first = {&trace, 1, first_finishes_first ? 1 : 2};
    struct worker_argument second = {&trace, 2, first_finishes_first ? 2 : 1};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, worker, &first));
    CHECK(scheduler_create(&scheduler, 1, worker, &second));
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_READY);
    CHECK(scheduler_state(&scheduler, 1) == SCHEDULER_READY);
    CHECK(scheduler_resume(&scheduler, 0));
    CHECK(scheduler_resume(&scheduler, 1));
    CHECK(scheduler_resume(&scheduler, 0));
    CHECK(scheduler_resume(&scheduler, 1));
    size_t final_index = first_finishes_first ? 1 : 0;
    CHECK(scheduler_state(&scheduler, first_finishes_first ? 0 : 1) ==
          SCHEDULER_FINISHED);
    CHECK(scheduler_state(&scheduler, final_index) == SCHEDULER_READY);
    CHECK(scheduler_resume(&scheduler, final_index));
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_FINISHED);
    CHECK(scheduler_state(&scheduler, 1) == SCHEDULER_FINISHED);
    CHECK(!scheduler_resume(&scheduler, final_index));
    CHECK(errno == EINVAL);
    if (first_finishes_first) {
        const int expected[] = {10, 20, 11, 22, 24};
        check_trace(&trace, expected, 5);
    } else {
        const int expected[] = {10, 20, 11, 22, 12};
        check_trace(&trace, expected, 5);
    }
    CHECK(scheduler_destroy(&scheduler));
    CHECK(scheduler_destroy(&scheduler));
}

struct allocation_probe {
    size_t attempts;
    size_t releases;
    size_t fail_on_attempt;
};

static void *probe_allocate(void *context, size_t bytes) {
    struct allocation_probe *probe = context;
    probe->attempts++;
    if (probe->attempts == probe->fail_on_attempt) {
        return NULL;
    }
    return malloc(bytes);
}

static void probe_deallocate(void *context, void *memory) {
    struct allocation_probe *probe = context;
    probe->releases++;
    free(memory);
}

static void test_partial_allocation_failure(void) {
    struct scheduler scheduler = {0};
    struct allocation_probe probe = {.fail_on_attempt = 2};
    struct trace trace = {0};
    struct worker_argument argument = {&trace, 1, 0};
    CHECK(scheduler_init(&scheduler, probe_allocate, probe_deallocate, &probe));
    CHECK(scheduler_create(&scheduler, 0, worker, &argument));
    CHECK(!scheduler_create(&scheduler, 1, worker, &argument));
    CHECK(errno == ENOMEM);
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_READY);
    CHECK(scheduler_state(&scheduler, 1) == SCHEDULER_EMPTY);
    CHECK(probe.attempts == 2 && probe.releases == 0);
    CHECK(scheduler_destroy(&scheduler));
    CHECK(probe.releases == 1);
    CHECK(scheduler_destroy(&scheduler));
    CHECK(probe.releases == 1);
}

static void test_invalid_calls(void) {
    struct scheduler scheduler = {0};
    CHECK(!scheduler_init(NULL, NULL, NULL, NULL));
    CHECK(!scheduler_init(&scheduler, probe_allocate, NULL, NULL));
    CHECK(!scheduler_yield());
    CHECK(!scheduler_create(&scheduler, 0, worker, NULL));
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(!scheduler_create(&scheduler, 2, worker, NULL));
    CHECK(!scheduler_create(&scheduler, 0, NULL, NULL));
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_EMPTY);
    CHECK(scheduler_destroy(&scheduler));
}

int main(void) {
    test_contexts(true);
    test_contexts(false);
    test_partial_allocation_failure();
    test_invalid_calls();
    puts("Runtime: contextos, retorno e falha parcial passaram (US04).");
    return EXIT_SUCCESS;
}
