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

#define TEST_EVENT_A 1u
#define TEST_EVENT_B 2u

static bool condition_ready(void *context);

struct trace {
    int values[8];
    size_t count;
};

struct worker_argument {
    struct trace *trace;
    int id;
    int yields;
    struct scheduler *scheduler;
};

static void worker(void *argument) {
    struct worker_argument *worker_argument = argument;
    int local = worker_argument->id * 10;
    for (int step = 0; step < worker_argument->yields; step++) {
        if (worker_argument->scheduler != NULL) {
            size_t own_index = (size_t)(worker_argument->id - 1);
            size_t other_index = 1 - own_index;
            CHECK(scheduler_state(worker_argument->scheduler, own_index) ==
                  SCHEDULER_RUNNING);
            CHECK(scheduler_state(worker_argument->scheduler, other_index) !=
                  SCHEDULER_RUNNING);
            CHECK(!scheduler_run(worker_argument->scheduler));
            CHECK(errno == EINVAL);
        }
        worker_argument->trace->values[worker_argument->trace->count++] = local;
        CHECK(scheduler_yield());
        local += worker_argument->id;
    }
    if (worker_argument->scheduler != NULL) {
        size_t own_index = (size_t)(worker_argument->id - 1);
        size_t other_index = 1 - own_index;
        CHECK(scheduler_state(worker_argument->scheduler, own_index) ==
              SCHEDULER_RUNNING);
        CHECK(scheduler_state(worker_argument->scheduler, other_index) !=
              SCHEDULER_RUNNING);
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
    struct worker_argument first = {&trace, 1, first_finishes_first ? 1 : 2, NULL};
    struct worker_argument second = {&trace, 2, first_finishes_first ? 2 : 1, NULL};
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
    struct worker_argument argument = {&trace, 1, 0, NULL};
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
    CHECK(!scheduler_run(&scheduler));
    CHECK(errno == EINVAL);
    CHECK(!scheduler_wait(&scheduler, TEST_EVENT_A, condition_ready, NULL));
    CHECK(errno == EINVAL);
    CHECK(scheduler_notify(&scheduler, SCHEDULER_NO_EVENT) == -1);
    CHECK(errno == EINVAL);
    CHECK(!scheduler_create(&scheduler, 2, worker, NULL));
    CHECK(!scheduler_create(&scheduler, 0, NULL, NULL));
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_EMPTY);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_round_robin(bool first_finishes_first) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    struct worker_argument first = {&trace, 1, first_finishes_first ? 1 : 2,
                                    &scheduler};
    struct worker_argument second = {&trace, 2, first_finishes_first ? 2 : 1,
                                     &scheduler};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, worker, &first));
    CHECK(scheduler_create(&scheduler, 1, worker, &second));
    CHECK(scheduler_run(&scheduler));
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_FINISHED);
    CHECK(scheduler_state(&scheduler, 1) == SCHEDULER_FINISHED);
    CHECK(scheduler_run(&scheduler));
    if (first_finishes_first) {
        const int expected[] = {10, 20, 11, 22, 24};
        check_trace(&trace, expected, 5);
    } else {
        const int expected[] = {10, 20, 11, 22, 12};
        check_trace(&trace, expected, 5);
    }
    CHECK(!scheduler_resume(&scheduler, 0));
    CHECK(errno == EINVAL);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_one_ready(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    struct worker_argument only = {&trace, 2, 2, &scheduler};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 1, worker, &only));
    CHECK(scheduler_run(&scheduler));
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_EMPTY);
    CHECK(scheduler_state(&scheduler, 1) == SCHEDULER_FINISHED);
    const int expected[] = {20, 22, 24};
    check_trace(&trace, expected, 3);
    CHECK(scheduler_destroy(&scheduler));
}

struct wait_argument {
    struct scheduler *scheduler;
    struct trace *trace;
    bool *condition;
    unsigned int event_id;
    int before;
    int after;
};

struct event_probe {
    struct scheduler *scheduler;
    bool *first;
    bool *second;
    size_t process_calls;
    size_t wait_calls;
};

struct condition_probe {
    struct scheduler *scheduler;
    struct trace *trace;
    size_t checks;
    bool ready;
};

static bool condition_ready(void *context) {
    return *(bool *)context;
}

static bool condition_changes_between_checks(void *context) {
    struct condition_probe *probe = context;
    probe->checks++;
    if (probe->checks == 1) {
        probe->ready = true;
        return false;
    }
    return probe->ready;
}

static void worker_with_changing_condition(void *context) {
    struct condition_probe *probe = context;
    probe->trace->values[probe->trace->count++] = 10;
    CHECK(scheduler_wait(probe->scheduler, TEST_EVENT_A,
                         condition_changes_between_checks, probe));
    probe->trace->values[probe->trace->count++] = 11;
}

static void waiting_worker(void *argument) {
    struct wait_argument *wait_argument = argument;
    wait_argument->trace->values[wait_argument->trace->count++] =
        wait_argument->before;
    CHECK(scheduler_wait(wait_argument->scheduler, wait_argument->event_id,
                         condition_ready, wait_argument->condition));
    CHECK(*wait_argument->condition);
    wait_argument->trace->values[wait_argument->trace->count++] =
        wait_argument->after;
}

static bool process_events(void *context) {
    struct event_probe *probe = context;
    probe->process_calls++;
    CHECK(scheduler_state(probe->scheduler, 0) != SCHEDULER_RUNNING);
    CHECK(scheduler_state(probe->scheduler, 1) != SCHEDULER_RUNNING);
    return true;
}

static bool process_wakes_before_idle(void *context) {
    struct event_probe *probe = context;
    CHECK(process_events(context));
    if (scheduler_state(probe->scheduler, 0) == SCHEDULER_WAITING) {
        *probe->first = true;
        CHECK(scheduler_notify(probe->scheduler, TEST_EVENT_A) == 1);
    }
    return true;
}

static bool wake_both_on_wait(void *context) {
    struct event_probe *probe = context;
    probe->wait_calls++;
    CHECK(probe->scheduler->ready_count == 0);
    CHECK(scheduler_state(probe->scheduler, 0) == SCHEDULER_WAITING);
    CHECK(scheduler_state(probe->scheduler, 1) == SCHEDULER_WAITING);
    *probe->first = true;
    CHECK(scheduler_notify(probe->scheduler, TEST_EVENT_A) == 2);
    return true;
}

static bool spurious_then_ready(void *context) {
    struct event_probe *probe = context;
    probe->wait_calls++;
    CHECK(probe->scheduler->ready_count == 0);
    if (probe->wait_calls == 2) {
        *probe->first = true;
    }
    CHECK(scheduler_notify(probe->scheduler, TEST_EVENT_A) == 1);
    return true;
}

static bool wake_distinct_events(void *context) {
    struct event_probe *probe = context;
    probe->wait_calls++;
    CHECK(probe->scheduler->ready_count == 0);
    if (probe->wait_calls == 1) {
        *probe->first = true;
        CHECK(scheduler_notify(probe->scheduler, TEST_EVENT_A) == 1);
        CHECK(scheduler_state(probe->scheduler, 1) == SCHEDULER_WAITING);
    } else {
        *probe->second = true;
        CHECK(scheduler_notify(probe->scheduler, TEST_EVENT_B) == 1);
    }
    return true;
}

static void notifying_worker(void *context) {
    struct event_probe *probe = context;
    *probe->first = true;
    CHECK(scheduler_notify(probe->scheduler, TEST_EVENT_A) == 1);
}

static bool unexpected_wait(void *context) {
    struct event_probe *probe = context;
    probe->wait_calls++;
    errno = EDEADLK;
    return false;
}

static bool failing_wait(void *context) {
    struct event_probe *probe = context;
    probe->wait_calls++;
    errno = EIO;
    return false;
}

static void test_waiting_together(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool ready = false;
    struct wait_argument first = {&scheduler, &trace, &ready, TEST_EVENT_A, 10, 11};
    struct wait_argument second = {&scheduler, &trace, &ready, TEST_EVENT_A, 20, 21};
    struct event_probe probe = {&scheduler, &ready, NULL, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &first));
    CHECK(scheduler_create(&scheduler, 1, waiting_worker, &second));
    CHECK(scheduler_run_events(&scheduler, process_events, wake_both_on_wait,
                               &probe));
    const int expected[] = {10, 20, 11, 21};
    check_trace(&trace, expected, 4);
    CHECK(probe.wait_calls == 1);
    CHECK(probe.process_calls == 6);
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_FINISHED);
    CHECK(scheduler_state(&scheduler, 1) == SCHEDULER_FINISHED);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_early_notification(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool ready = true;
    struct wait_argument only = {&scheduler, &trace, &ready, TEST_EVENT_A, 10, 11};
    struct event_probe probe = {&scheduler, &ready, NULL, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_notify(&scheduler, TEST_EVENT_A) == 0);
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &only));
    CHECK(scheduler_run_events(&scheduler, process_events, wake_both_on_wait,
                               &probe));
    const int expected[] = {10, 11};
    check_trace(&trace, expected, 2);
    CHECK(probe.wait_calls == 0);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_condition_changes_before_suspend(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    struct condition_probe probe = {&scheduler, &trace, 0, false};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, worker_with_changing_condition,
                           &probe));
    CHECK(scheduler_run(&scheduler));
    const int expected[] = {10, 11};
    check_trace(&trace, expected, 2);
    CHECK(probe.checks == 2);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_spurious_notification(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool ready = false;
    struct wait_argument only = {&scheduler, &trace, &ready, TEST_EVENT_A, 10, 11};
    struct event_probe probe = {&scheduler, &ready, NULL, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &only));
    CHECK(scheduler_run_events(&scheduler, process_events, spurious_then_ready,
                               &probe));
    const int expected[] = {10, 11};
    check_trace(&trace, expected, 2);
    CHECK(probe.wait_calls == 2);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_distinct_events(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool first_ready = false;
    bool second_ready = false;
    struct wait_argument first = {&scheduler, &trace, &first_ready,
                                  TEST_EVENT_A, 10, 11};
    struct wait_argument second = {&scheduler, &trace, &second_ready,
                                   TEST_EVENT_B, 20, 21};
    struct event_probe probe = {&scheduler, &first_ready, &second_ready, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &first));
    CHECK(scheduler_create(&scheduler, 1, waiting_worker, &second));
    CHECK(scheduler_run_events(&scheduler, process_events, wake_distinct_events,
                               &probe));
    const int expected[] = {10, 20, 11, 21};
    check_trace(&trace, expected, 4);
    CHECK(probe.wait_calls == 2);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_wait_without_callback(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool ready = false;
    struct wait_argument only = {&scheduler, &trace, &ready, TEST_EVENT_A, 10, 11};
    struct event_probe probe = {&scheduler, &ready, NULL, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &only));
    CHECK(!scheduler_run(&scheduler));
    CHECK(errno == EAGAIN);
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_WAITING);
    CHECK(scheduler_run_events(&scheduler, process_events, spurious_then_ready,
                               &probe));
    CHECK(probe.wait_calls == 2);
    const int expected[] = {10, 11};
    check_trace(&trace, expected, 2);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_notification_from_running_worker(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool ready = false;
    struct wait_argument waiting = {&scheduler, &trace, &ready,
                                    TEST_EVENT_A, 10, 11};
    struct event_probe probe = {&scheduler, &ready, NULL, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &waiting));
    CHECK(scheduler_create(&scheduler, 1, notifying_worker, &probe));
    CHECK(scheduler_run_events(&scheduler, process_events, unexpected_wait,
                               &probe));
    const int expected[] = {10, 11};
    check_trace(&trace, expected, 2);
    CHECK(probe.wait_calls == 0);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_event_processed_before_idle(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool ready = false;
    struct wait_argument waiting = {&scheduler, &trace, &ready,
                                    TEST_EVENT_A, 10, 11};
    struct event_probe probe = {&scheduler, &ready, NULL, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &waiting));
    CHECK(scheduler_run_events(&scheduler, process_wakes_before_idle,
                               unexpected_wait, &probe));
    const int expected[] = {10, 11};
    check_trace(&trace, expected, 2);
    CHECK(probe.wait_calls == 0);
    CHECK(scheduler_destroy(&scheduler));
}

static void test_wait_callback_failure(void) {
    struct scheduler scheduler = {0};
    struct trace trace = {0};
    bool ready = false;
    struct wait_argument waiting = {&scheduler, &trace, &ready,
                                    TEST_EVENT_A, 10, 11};
    struct event_probe probe = {&scheduler, &ready, NULL, 0, 0};
    CHECK(scheduler_init(&scheduler, NULL, NULL, NULL));
    CHECK(scheduler_create(&scheduler, 0, waiting_worker, &waiting));
    CHECK(!scheduler_run_events(&scheduler, process_events, failing_wait,
                                &probe));
    CHECK(errno == EIO);
    CHECK(probe.wait_calls == 1);
    CHECK(scheduler_state(&scheduler, 0) == SCHEDULER_WAITING);
    ready = true;
    CHECK(scheduler_notify(&scheduler, TEST_EVENT_A) == 1);
    CHECK(scheduler_run(&scheduler));
    const int expected[] = {10, 11};
    check_trace(&trace, expected, 2);
    CHECK(scheduler_destroy(&scheduler));
}

int main(void) {
    test_contexts(true);
    test_contexts(false);
    test_partial_allocation_failure();
    test_invalid_calls();
    test_round_robin(true);
    test_round_robin(false);
    test_one_ready();
    test_waiting_together();
    test_early_notification();
    test_condition_changes_before_suspend();
    test_spurious_notification();
    test_distinct_events();
    test_wait_without_callback();
    test_notification_from_running_worker();
    test_event_processed_before_idle();
    test_wait_callback_failure();
    puts("Runtime: contextos, round-robin e eventos passaram (US04-US06).");
    return EXIT_SUCCESS;
}
