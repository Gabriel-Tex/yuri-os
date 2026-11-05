#include "domain/client.h"

#include <stdio.h>
#include <stdlib.h>

/* As verificações continuam ativas mesmo se o chamador compilar com NDEBUG. */
#define CHECK(condition)                                                            \
    do                                                                              \
    {                                                                               \
        if (!(condition))                                                           \
        {                                                                           \
            fprintf(stderr, "%s:%d: falhou: %s\n", __FILE__, __LINE__, #condition); \
            exit(EXIT_FAILURE);                                                     \
        }                                                                           \
    } while (0)

static struct client_record make_client(void)
{
    struct client_record client;
    CHECK(client_init(&client, 123, 7, CLIENT_PRIORITY_HIGH, 1000, 5));
    return client;
}

static void check_equal(const struct client_record *actual,
                        const struct client_record *expected)
{
    CHECK(actual->pid == expected->pid);
    CHECK(actual->sequence == expected->sequence);
    CHECK(actual->priority == expected->priority);
    CHECK(actual->arrival_ns == expected->arrival_ns);
    CHECK(actual->patience_ns == expected->patience_ns);
    CHECK(actual->deadline_ns == expected->deadline_ns);
    CHECK(actual->demand_us == expected->demand_us);
    CHECK(actual->state == expected->state);
    CHECK(actual->completion_ns == expected->completion_ns);
}

static void test_priorities_and_units(void)
{
    const struct
    {
        uint64_t max_patience_ms;
        uint64_t high_ns;
        uint64_t low_ns;
    } cases[] = {
        {1, 500000, 1000000},
        {4, 2000000, 4000000},
        {5, 2500000, 5000000}};

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++)
    {
        struct client_record high;
        struct client_record low;
        CHECK(client_init(&high, 123, 0, CLIENT_PRIORITY_HIGH, 17,
                          cases[i].max_patience_ms));
        CHECK(client_init(&low, 456, UINT64_MAX, CLIENT_PRIORITY_LOW, 17,
                          cases[i].max_patience_ms));
        CHECK(high.patience_ns == cases[i].high_ns);
        CHECK(low.patience_ns == cases[i].low_ns);
        CHECK(high.deadline_ns == 17 + cases[i].high_ns);
        CHECK(low.deadline_ns == 17 + cases[i].low_ns);
        CHECK(high.priority == CLIENT_PRIORITY_HIGH);
        CHECK(low.priority == CLIENT_PRIORITY_LOW);
        CHECK(high.sequence == 0);
        CHECK(low.sequence == UINT64_MAX);
    }
}

static void test_initialization_limits(void)
{
    const uint64_t max_ms = UINT64_MAX / UINT64_C(1000000);
    struct client_record client = make_client();
    const struct client_record original = client;

    CHECK(!client_init(NULL, 123, 7, CLIENT_PRIORITY_HIGH, 0, 5));
    CHECK(!client_init(&client, 0, 7, CLIENT_PRIORITY_HIGH, 0, 5));
    check_equal(&client, &original);
    CHECK(!client_init(&client, -1, 7, CLIENT_PRIORITY_HIGH, 0, 5));
    check_equal(&client, &original);
    CHECK(!client_init(&client, 123, 7, (enum client_priority)99, 0, 5));
    check_equal(&client, &original);
    CHECK(!client_init(&client, 123, 7, CLIENT_PRIORITY_HIGH, 0, 0));
    check_equal(&client, &original);

    const enum client_priority priorities[] = {
        CLIENT_PRIORITY_HIGH, CLIENT_PRIORITY_LOW};
    for (size_t i = 0; i < sizeof priorities / sizeof priorities[0]; i++)
    {
        CHECK(!client_init(&client, 123, 7, priorities[i], 0, max_ms + 1));
        check_equal(&client, &original);
        CHECK(!client_init(&client, 123, 7, priorities[i], 0, UINT64_MAX));
        check_equal(&client, &original);

        struct client_record boundary;
        CHECK(client_init(&boundary, 123, 7, priorities[i], 0, max_ms));
        uint64_t patience_ns = max_ms * UINT64_C(1000000);
        if (priorities[i] == CLIENT_PRIORITY_HIGH)
        {
            patience_ns /= 2;
        }
        CHECK(boundary.patience_ns == patience_ns);
        uint64_t last_arrival_ns = UINT64_MAX - patience_ns;
        CHECK(client_init(&boundary, 123, 7, priorities[i], last_arrival_ns,
                          max_ms));
        CHECK(boundary.deadline_ns == UINT64_MAX);
        CHECK(!client_init(&client, 123, 7, priorities[i], last_arrival_ns + 1,
                           max_ms));
        check_equal(&client, &original);
        CHECK(!client_init(&client, 123, 7, priorities[i], UINT64_MAX, 1));
        check_equal(&client, &original);
    }
}

static void test_lifecycle(void)
{
    struct client_record client = make_client();
    CHECK(client.pid == 123);
    CHECK(client.sequence == 7);
    CHECK(client.state == CLIENT_STARTING);
    CHECK(client.demand_us == 0);
    CHECK(client.completion_ns == 0);
    const uint64_t arrival_ns = client.arrival_ns;
    const uint64_t deadline_ns = client.deadline_ns;

    CHECK(client_enqueue(&client, 123456));
    CHECK(client.state == CLIENT_QUEUED);
    CHECK(client.demand_us == 123456);
    CHECK(client.arrival_ns == arrival_ns);
    CHECK(client_start(&client));
    CHECK(client.state == CLIENT_IN_SERVICE);
    CHECK(client.arrival_ns == arrival_ns);
    struct client_record before_completion = client;
    CHECK(!client_complete(&client, arrival_ns - 1));
    check_equal(&client, &before_completion);
    /* Prazo vencido não impede concluir: a satisfação será calculada em US03. */
    CHECK(client_complete(&client, deadline_ns + 1));
    CHECK(client.state == CLIENT_COMPLETED);
    CHECK(client.arrival_ns == arrival_ns);
    CHECK(client.deadline_ns == deadline_ns);
    CHECK(client.demand_us == 123456);
    CHECK(client.completion_ns == deadline_ns + 1);

    client = make_client();
    CHECK(client_enqueue(&client, 0));
    CHECK(client_start(&client));
    CHECK(client_complete(&client, client.arrival_ns));

    client = make_client();
    CHECK(client_enqueue(&client, UINT64_MAX));
    CHECK(client.demand_us == UINT64_MAX);
    CHECK(client_start(&client));
    CHECK(client_complete(&client, UINT64_MAX));
    CHECK(client.completion_ns == UINT64_MAX);
}

static void test_transition_matrix(void)
{
    const enum client_state states[] = {
        CLIENT_STARTING, CLIENT_QUEUED, CLIENT_IN_SERVICE,
        CLIENT_COMPLETED, CLIENT_CANCELLED, CLIENT_FAILED,
        (enum client_state)99};

    for (size_t i = 0; i < sizeof states / sizeof states[0]; i++)
    {
        struct client_record original = make_client();
        original.state = states[i];
        struct client_record client = original;
        struct client_record expected = original;
        bool allowed = states[i] == CLIENT_STARTING;
        CHECK(client_enqueue(&client, 42) == allowed);
        if (allowed)
        {
            expected.state = CLIENT_QUEUED;
            expected.demand_us = 42;
        }
        check_equal(&client, &expected);

        client = original;
        expected = original;
        allowed = states[i] == CLIENT_QUEUED;
        CHECK(client_start(&client) == allowed);
        if (allowed)
        {
            expected.state = CLIENT_IN_SERVICE;
        }
        check_equal(&client, &expected);

        client = original;
        expected = original;
        allowed = states[i] == CLIENT_IN_SERVICE;
        CHECK(client_complete(&client, 2000) == allowed);
        if (allowed)
        {
            expected.state = CLIENT_COMPLETED;
            expected.completion_ns = 2000;
        }
        check_equal(&client, &expected);

        client = original;
        expected = original;
        allowed = states[i] == CLIENT_STARTING || states[i] == CLIENT_QUEUED;
        CHECK(client_cancel(&client) == allowed);
        if (allowed)
        {
            expected.state = CLIENT_CANCELLED;
        }
        check_equal(&client, &expected);

        client = original;
        expected = original;
        allowed = states[i] == CLIENT_STARTING || states[i] == CLIENT_QUEUED ||
                  states[i] == CLIENT_IN_SERVICE;
        CHECK(client_fail(&client) == allowed);
        if (allowed)
        {
            expected.state = CLIENT_FAILED;
        }
        check_equal(&client, &expected);
    }

    CHECK(!client_enqueue(NULL, 42));
    CHECK(!client_start(NULL));
    CHECK(!client_complete(NULL, 2000));
    CHECK(!client_cancel(NULL));
    CHECK(!client_fail(NULL));
}

static void test_terminal_repetition(void)
{
    for (int terminal = 0; terminal < 3; terminal++)
    {
        struct client_record client = make_client();
        if (terminal == 0)
        {
            CHECK(client_enqueue(&client, 42));
            CHECK(client_start(&client));
            CHECK(client_complete(&client, 2000));
        }
        else if (terminal == 1)
        {
            CHECK(client_cancel(&client));
        }
        else
        {
            CHECK(client_fail(&client));
        }
        const struct client_record original = client;
        CHECK(!client_complete(&client, 3000));
        CHECK(!client_cancel(&client));
        CHECK(!client_fail(&client));
        CHECK(!client_enqueue(&client, 99));
        CHECK(!client_start(&client));
        check_equal(&client, &original);
    }
}

int main(void)
{
    test_priorities_and_units();
    test_initialization_limits();
    test_lifecycle();
    test_transition_matrix();
    test_terminal_repetition();
    puts("Domínio: 5 grupos de testes passaram (US01).");
    return EXIT_SUCCESS;
}
