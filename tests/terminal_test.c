#define _GNU_SOURCE
#include "platform/terminal.h"

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_pipe(void)
{
    int ends[2];
    assert(pipe(ends) == 0);
    int flags = fcntl(ends[0], F_GETFL);
    struct terminal_input input = {0};
    assert(terminal_init(&input, ends[0]));
    bool stop;
    bool eof;
    assert(terminal_poll(&input, &stop, &eof) && !stop && !eof);
    assert(write(ends[1], "xS", 2) == 2);
    assert(terminal_poll(&input, &stop, &eof) && !stop && !eof);
    assert(write(ends[1], "as", 2) == 2);
    assert(terminal_poll(&input, &stop, &eof) && stop && !eof);
    assert(close(ends[1]) == 0);
    assert(terminal_poll(&input, &stop, &eof) && !stop && eof);
    assert(terminal_poll(&input, &stop, &eof) && !stop && eof);
    assert(terminal_close(&input));
    assert(terminal_close(&input));
    assert(fcntl(ends[0], F_GETFL) == flags);
    assert(close(ends[0]) == 0);
}

static void test_pty(void)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0);
    assert(grantpt(master) == 0 && unlockpt(master) == 0);
    char *name = ptsname(master);
    assert(name != NULL);
    int slave = open(name, O_RDWR | O_NOCTTY);
    assert(slave >= 0);
    struct termios original;
    assert(tcgetattr(slave, &original) == 0);
    int flags = fcntl(slave, F_GETFL);
    struct terminal_input input = {0};
    assert(terminal_init(&input, slave));
    struct termios active;
    assert(tcgetattr(slave, &active) == 0);
    assert((active.c_lflag & (ICANON | ECHO)) == 0);
    assert(write(master, "zs", 2) == 2);
    bool stop;
    bool eof;
    assert(terminal_poll(&input, &stop, &eof) && stop && !eof);
    assert(terminal_close(&input));
    assert(tcgetattr(slave, &active) == 0);
    assert(active.c_iflag == original.c_iflag &&
           active.c_oflag == original.c_oflag &&
           active.c_cflag == original.c_cflag &&
           active.c_lflag == original.c_lflag &&
           memcmp(active.c_cc, original.c_cc, NCCS) == 0);
    assert(fcntl(slave, F_GETFL) == flags);
    assert(close(slave) == 0 && close(master) == 0);
}

int main(void)
{
    test_pipe();
    test_pty();
    struct terminal_input input = {0};
    assert(!terminal_init(&input, -1));
    assert(!input.initialized);
    return 0;
}
