#define _GNU_SOURCE
#include "terminal.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

bool terminal_init(struct terminal_input *input, int descriptor)
{
    if (input == NULL || descriptor < 0 || input->initialized ||
        input->flags_saved || input->attributes_saved) {
        errno = EINVAL;
        return false;
    }
    input->descriptor = descriptor;
    int flags = fcntl(descriptor, F_GETFL);
    if (flags == -1) return false;
    input->original_flags = flags;
    input->flags_saved = true;
    if (isatty(descriptor)) {
        if (tcgetattr(descriptor, &input->original_attributes) == -1)
            goto fail;
        input->attributes_saved = true;
        struct termios attributes = input->original_attributes;
        attributes.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        attributes.c_cc[VMIN] = 1;
        attributes.c_cc[VTIME] = 0;
        if (tcsetattr(descriptor, TCSANOW, &attributes) == -1) goto fail;
    }
    if (fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == -1) goto fail;
    input->initialized = true;
    input->eof = false;
    return true;
fail: {
        int error = errno;
        terminal_close(input);
        errno = error;
        return false;
    }
}

bool terminal_poll(struct terminal_input *input, bool *stop_requested,
                   bool *end_of_input)
{
    if (input == NULL || !input->initialized || stop_requested == NULL ||
        end_of_input == NULL) {
        errno = EINVAL;
        return false;
    }
    *stop_requested = false;
    *end_of_input = input->eof;
    if (input->eof) return true;
    char bytes[256];
    /* Um bloco por passagem permite processar filhos e despachar as rotinas
     * mesmo quando a fonte de entrada permanece continuamente pronta. */
    ssize_t count = read(input->descriptor, bytes, sizeof(bytes));
    if (count > 0) {
        for (ssize_t i = 0; i < count; i++) {
            if (bytes[i] == 's') *stop_requested = true;
        }
        return true;
    }
    if (count == 0) {
        input->eof = true;
        *end_of_input = true;
        return true;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        return true;
    return false;
}

bool terminal_close(struct terminal_input *input)
{
    if (input == NULL) {
        errno = EINVAL;
        return false;
    }
    int error = 0;
    if (input->attributes_saved) {
        if (tcsetattr(input->descriptor, TCSANOW,
                      &input->original_attributes) == -1) error = errno;
        else input->attributes_saved = false;
    }
    if (input->flags_saved) {
        if (fcntl(input->descriptor, F_SETFL, input->original_flags) == -1) {
            if (error == 0) error = errno;
        } else {
            input->flags_saved = false;
        }
    }
    if (error != 0) {
        errno = error;
        return false;
    }
    input->initialized = false;
    input->eof = false;
    return true;
}
