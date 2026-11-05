#define _POSIX_C_SOURCE 200809L
#include "clock.h"

#include <errno.h>
#include <stddef.h>

#define CLOCK_NS_PER_S UINT64_C(1000000000)

bool clock_timespec_ns(const struct timespec *time, uint64_t *nanoseconds)
{
    if (time == NULL || nanoseconds == NULL || time->tv_sec < 0 ||
        time->tv_nsec < 0 || time->tv_nsec >= (long)CLOCK_NS_PER_S)
    {
        errno = EINVAL;
        return false;
    }
    uint64_t seconds = (uint64_t)time->tv_sec;
    if (seconds > (UINT64_MAX - (uint64_t)time->tv_nsec) / CLOCK_NS_PER_S)
    {
        errno = ERANGE;
        return false;
    }
    *nanoseconds = seconds * CLOCK_NS_PER_S + (uint64_t)time->tv_nsec;
    return true;
}

bool clock_monotonic_ns(uint64_t *nanoseconds)
{
    if (nanoseconds == NULL)
    {
        errno = EINVAL;
        return false;
    }
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return false;
    }
    return clock_timespec_ns(&now, nanoseconds);
}

bool clock_elapsed_ns(uint64_t start_ns, uint64_t end_ns, uint64_t *elapsed_ns)
{
    if (elapsed_ns == NULL || end_ns < start_ns)
    {
        errno = EINVAL;
        return false;
    }
    *elapsed_ns = end_ns - start_ns;
    return true;
}
