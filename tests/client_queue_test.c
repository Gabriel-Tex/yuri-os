#include "domain/client_queue.h"

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

static struct client_record make_client(pid_t pid, uint64_t sequence,
                                        uint64_t arrival_ns)
{
    struct client_record client;
    CHECK(client_init(&client, pid, sequence, CLIENT_PRIORITY_LOW,
                      arrival_ns, 1));
    CHECK(client_enqueue(&client, 25));
    return client;
}

static void test_empty_and_single(void)
{
    struct client_queue queue;
    struct client_record result = make_client(999, 999, 0);
    CHECK(client_queue_init(&queue));
    CHECK(client_queue_waiting_count(&queue) == 0);
    CHECK(!client_queue_take_next(&queue, &result));
    CHECK(result.pid == 999);
    CHECK(client_queue_has_space(&queue));
    CHECK(client_queue_reserve(&queue));
    CHECK(client_queue_waiting_count(&queue) == 1);
    CHECK(!client_queue_reserve(&queue));

    struct client_record starting;
    CHECK(client_init(&starting, 1, 1, CLIENT_PRIORITY_HIGH, 0, 1));
    CHECK(!client_queue_insert(&queue, &starting));
    CHECK(client_queue_waiting_count(&queue) == 1);
    CHECK(client_enqueue(&starting, 0));
    CHECK(client_queue_insert(&queue, &starting));
    CHECK(client_queue_waiting_count(&queue) == 1);
    CHECK(client_queue_has_space(&queue));
    CHECK(client_queue_take_next(&queue, &result));
    CHECK(result.pid == 1 && result.state == CLIENT_QUEUED);
    CHECK(result.demand_us == 0);
    CHECK(client_start(&result));
    CHECK(client_queue_waiting_count(&queue) == 0);
    CHECK(!client_queue_take_next(&queue, &result));
    CHECK(result.pid == 1);
}

static void test_capacity_and_reuse(void)
{
    struct client_queue queue;
    struct client_record result;
    CHECK(client_queue_init(&queue));
    for (size_t i = 0; i < CLIENT_QUEUE_CAPACITY - 1; i++)
    {
        struct client_record client = make_client((pid_t)(i + 1), i, i);
        CHECK(client_queue_reserve(&queue));
        CHECK(client_queue_insert(&queue, &client));
    }
    CHECK(client_queue_waiting_count(&queue) == 99);
    CHECK(client_queue_reserve(&queue));
    CHECK(client_queue_waiting_count(&queue) == 100);
    CHECK(!client_queue_has_space(&queue));
    CHECK(!client_queue_reserve(&queue));

    struct client_record last = make_client(100, 99, 99);
    CHECK(client_queue_insert(&queue, &last));
    CHECK(client_queue_waiting_count(&queue) == 100);
    CHECK(!client_queue_reserve(&queue));
    CHECK(client_queue_take_next(&queue, &result));
    CHECK(result.pid == 1);
    CHECK(client_queue_waiting_count(&queue) == 99);
    CHECK(client_queue_reserve(&queue));
    CHECK(client_queue_waiting_count(&queue) == 100);
    CHECK(client_queue_release_reservation(&queue));
    CHECK(!client_queue_release_reservation(&queue));
    CHECK(client_queue_waiting_count(&queue) == 99);
    CHECK(client_queue_reserve(&queue));
    struct client_record replacement = make_client(101, 100, 100);
    CHECK(client_queue_insert(&queue, &replacement));

    for (size_t i = 2; i <= 101; i++)
    {
        CHECK(client_queue_take_next(&queue, &result));
        CHECK(result.pid == (pid_t)i);
        CHECK(client_start(&result));
    }
    CHECK(client_queue_waiting_count(&queue) == 0);
}

static void test_order_and_expired_deadline(void)
{
    struct client_queue queue;
    struct client_record result;
    CHECK(client_queue_init(&queue));
    const struct
    {
        pid_t pid;
        uint64_t sequence;
        uint64_t arrival_ns;
    } cases[] = {
        {10, 10, 500}, {30, 30, 100}, {20, 20, 100}, {40, 40, 900}};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        struct client_record client = make_client(cases[i].pid,
                                                  cases[i].sequence,
                                                  cases[i].arrival_ns);
        CHECK(client_queue_reserve(&queue));
        CHECK(client_queue_insert(&queue, &client));
    }
    const pid_t expected[] = {20, 30, 10, 40};
    for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++)
    {
        CHECK(client_queue_take_next(&queue, &result));
        CHECK(result.pid == expected[i]);
    }
    CHECK(!client_queue_take_next(&queue, &result));

    struct client_record expired = make_client(50, 50, 0);
    const uint64_t now_ns = UINT64_C(2000000);
    CHECK(expired.deadline_ns < now_ns);
    CHECK(client_queue_reserve(&queue));
    CHECK(client_queue_insert(&queue, &expired));
    CHECK(client_queue_take_next(&queue, &result));
    CHECK(result.pid == 50 && result.deadline_ns == UINT64_C(1000000));
    /* A retirada não recebe relógio e não elimina clientes pelo prazo. */
}

static void test_invalid_operations(void)
{
    struct client_queue queue;
    struct client_record result = make_client(999, 999, 0);
    struct client_record client = make_client(1, 1, 0);
    CHECK(!client_queue_init(NULL));
    CHECK(client_queue_init(&queue));
    CHECK(client_queue_waiting_count(NULL) == 0);
    CHECK(!client_queue_has_space(NULL));
    CHECK(!client_queue_reserve(NULL));
    CHECK(!client_queue_release_reservation(NULL));
    CHECK(!client_queue_insert(NULL, &client));
    CHECK(!client_queue_insert(&queue, &client));
    CHECK(!client_queue_take_next(NULL, &result));
    CHECK(!client_queue_take_next(&queue, NULL));
    CHECK(client_queue_reserve(&queue));
    CHECK(!client_queue_insert(&queue, NULL));
    CHECK(client_queue_waiting_count(&queue) == 1);
    CHECK(client_queue_insert(&queue, &client));
    CHECK(!client_queue_insert(&queue, &client));
    CHECK(client_queue_take_next(&queue, &result));
    CHECK(result.pid == 1);
}

int main(void)
{
    test_empty_and_single();
    test_capacity_and_reuse();
    test_order_and_expired_deadline();
    test_invalid_operations();
    puts("Fila: 4 grupos de testes passaram (US02).");
    return EXIT_SUCCESS;
}
