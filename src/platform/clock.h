#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Todos os instantes são nanossegundos do CLOCK_MONOTONIC. */
bool clock_timespec_ns(const struct timespec *time, uint64_t *nanoseconds);
bool clock_monotonic_ns(uint64_t *nanoseconds);
bool clock_elapsed_ns(uint64_t start_ns, uint64_t end_ns, uint64_t *elapsed_ns);

#endif
