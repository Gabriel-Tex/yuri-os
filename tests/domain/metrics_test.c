#include "domain/metrics.h"
#include "platform/clock.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                            \
    do                                                                              \
    {                                                                               \
        if (!(condition))                                                           \
        {                                                                           \
            fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                                     \
        }                                                                           \
    } while (0)

static struct client_record make_client(pid_t pid, enum client_priority priority,
                                        uint64_t arrival_ns)
{
    struct client_record client;
    CHECK(client_init(&client, pid, (uint64_t)pid, priority, arrival_ns, 5));
    return client;
}

static void test_response_boundaries(void)
{
    struct metrics metrics;
    struct client_record equal = make_client(1, CLIENT_PRIORITY_HIGH, 1000000);
    struct client_record late = make_client(2, CLIENT_PRIORITY_HIGH, 1000000);
    struct client_record low = make_client(3, CLIENT_PRIORITY_LOW, 1000000);
    CHECK(metrics_init(&metrics));
    CHECK(metrics_record_created(&metrics));
    CHECK(metrics_record_created(&metrics));
    CHECK(metrics_record_created(&metrics));
    CHECK(client_enqueue(&equal, 100));
    CHECK(client_start(&equal));
    CHECK(client_enqueue(&late, 100));
    CHECK(client_start(&late));
    CHECK(client_enqueue(&low, 100));
    CHECK(client_start(&low));
    CHECK(equal.patience_ns == 2500000);
    CHECK(metrics_record_completed(&metrics, &equal, 3500000));
    CHECK(metrics.satisfied == 1);
    CHECK(metrics_record_completed(&metrics, &late, 3500001));
    CHECK(metrics.satisfied == 1);
    CHECK(metrics_record_completed(&metrics, &low, 6000000));
    CHECK(metrics.satisfied == 2);
    CHECK(metrics.completed == 3);
    double rate = -1;
    CHECK(metrics_finite_rate(&metrics, 3, &rate));
    CHECK(rate == 2.0 / 3.0);
    CHECK(metrics_infinite_rate(&metrics, &rate));
    CHECK(rate == 2.0 / 3.0);

    struct metrics previous = metrics;
    CHECK(!metrics_record_completed(&metrics, &equal, 3500000));
    CHECK(!metrics_record_cancelled(&metrics, &equal));
    CHECK(!metrics_record_failed(&metrics, &equal));
    CHECK(metrics.completed == previous.completed);
    CHECK(metrics.satisfied == previous.satisfied);
    CHECK(metrics.cancelled == previous.cancelled);
    CHECK(metrics.failed == previous.failed);
}

static void test_cancellation_and_incomplete_finite(void)
{
    struct metrics metrics;
    struct client_record completed = make_client(1, CLIENT_PRIORITY_LOW, 0);
    struct client_record waiting = make_client(2, CLIENT_PRIORITY_HIGH, 0);
    CHECK(metrics_init(&metrics));
    CHECK(metrics_record_created(&metrics));
    CHECK(metrics_record_created(&metrics));
    CHECK(client_enqueue(&completed, 5));
    CHECK(client_start(&completed));
    CHECK(metrics_record_completed(&metrics, &completed, 5000000));
    CHECK(metrics_record_cancelled(&metrics, &waiting));
    CHECK(waiting.state == CLIENT_CANCELLED);
    CHECK(metrics.created == 2 && metrics.completed == 1);
    CHECK(metrics.cancelled == 1 && metrics.satisfied == 1);
    double rate = -1;
    CHECK(!metrics_finite_rate(&metrics, 2, &rate));
    CHECK(rate == -1);
    CHECK(metrics_infinite_rate(&metrics, &rate));
    CHECK(rate == 1.0);
    CHECK(!metrics_record_cancelled(&metrics, &waiting));

    struct metrics empty;
    CHECK(metrics_init(&empty));
    rate = -1;
    CHECK(!metrics_infinite_rate(&empty, &rate));
    CHECK(rate == -1);
    CHECK(!metrics_finite_rate(&empty, 0, &rate));
    CHECK(rate == -1);
    CHECK(!metrics_finite_rate(&empty, 2, &rate));
}

static void test_failures_and_invalid_events(void)
{
    struct metrics metrics;
    struct client_record client = make_client(1, CLIENT_PRIORITY_LOW, 100);
    CHECK(metrics_init(&metrics));
    CHECK(!metrics_record_completed(&metrics, &client, 100));
    CHECK(client.state == CLIENT_STARTING);
    CHECK(metrics_record_created(&metrics));
    CHECK(!metrics_record_completed(&metrics, &client, 100));
    CHECK(client_enqueue(&client, 0));
    CHECK(client_start(&client));
    CHECK(!metrics_record_completed(&metrics, &client, 99));
    CHECK(client.state == CLIENT_IN_SERVICE && metrics.completed == 0);
    CHECK(!metrics_record_cancelled(&metrics, &client));
    CHECK(metrics_record_failed(&metrics, &client));
    CHECK(client.state == CLIENT_FAILED && metrics.failed == 1);
    CHECK(!metrics_record_failed(&metrics, &client));
    CHECK(!metrics_record_completed(&metrics, &client, 200));
    CHECK(!metrics_record_cancelled(&metrics, &client));
    CHECK(metrics.completed == 0 && metrics.failed == 1);
    double rate = -1;
    CHECK(!metrics_finite_rate(&metrics, 1, &rate));
    CHECK(!metrics_infinite_rate(&metrics, &rate));
    CHECK(rate == -1);

    CHECK(!metrics_init(NULL));
    CHECK(!metrics_record_created(NULL));
    CHECK(!metrics_record_completed(NULL, &client, 200));
    CHECK(!metrics_record_cancelled(NULL, &client));
    CHECK(!metrics_record_failed(NULL, &client));
    CHECK(!metrics_record_completed(&metrics, NULL, 200));
    CHECK(!metrics_record_cancelled(&metrics, NULL));
    CHECK(!metrics_record_failed(&metrics, NULL));
    CHECK(!metrics_finite_rate(NULL, 1, &rate));
    CHECK(!metrics_infinite_rate(NULL, &rate));
    CHECK(!metrics_finite_rate(&metrics, 1, NULL));
    CHECK(!metrics_infinite_rate(&metrics, NULL));

    metrics.created = UINT64_MAX;
    CHECK(!metrics_record_created(&metrics));
    CHECK(metrics.created == UINT64_MAX);
    metrics.completed = UINT64_MAX;
    metrics.cancelled = UINT64_MAX;
    CHECK(!metrics_record_failed(&metrics, &client));
    CHECK(metrics.failed == 1);
}

static void test_clock_conversions(void)
{
    uint64_t value = 42;
    struct timespec time = {.tv_sec = 2, .tv_nsec = 500000000};
    CHECK(clock_timespec_ns(&time, &value));
    CHECK(value == 2500000000);
    CHECK(clock_elapsed_ns(100, 2500000100, &value));
    CHECK(value == 2500000000);
    CHECK(clock_elapsed_ns(100, 100, &value));
    CHECK(value == 0);
    CHECK(!clock_elapsed_ns(101, 100, &value));
    CHECK(errno == EINVAL && value == 0);

    time.tv_sec = (time_t)(UINT64_MAX / UINT64_C(1000000000));
    time.tv_nsec = (long)(UINT64_MAX % UINT64_C(1000000000));
    CHECK(clock_timespec_ns(&time, &value));
    CHECK(value == UINT64_MAX);
    time.tv_sec++;
    CHECK(!clock_timespec_ns(&time, &value));
    CHECK(errno == ERANGE && value == UINT64_MAX);
    time.tv_sec = 0;
    time.tv_nsec = 1000000000;
    CHECK(!clock_timespec_ns(&time, &value));
    CHECK(errno == EINVAL && value == UINT64_MAX);
    time.tv_nsec = 0;
    time.tv_sec = -1;
    CHECK(!clock_timespec_ns(&time, &value));
    CHECK(!clock_timespec_ns(NULL, &value));
    CHECK(!clock_timespec_ns(&time, NULL));
    CHECK(!clock_elapsed_ns(0, 1, NULL));
    CHECK(!clock_monotonic_ns(NULL));
    /* Não consultar o relógio real: os demais testes usam apenas instantes dados. */
}

int main(void)
{
    test_response_boundaries();
    test_cancellation_and_incomplete_finite();
    test_failures_and_invalid_events();
    test_clock_conversions();
    puts("Métricas e conversões: 4 grupos passaram (US03).");
    return EXIT_SUCCESS;
}
