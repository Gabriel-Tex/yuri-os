#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>
#include <termios.h>

struct terminal_input {
    int descriptor;
    int original_flags;
    struct termios original_attributes;
    bool flags_saved;
    bool attributes_saved;
    bool initialized;
    bool eof;
};

/* Mantém a entrada não bloqueante; em TTY, desativa o modo canônico e o eco. */
bool terminal_init(struct terminal_input *input, int descriptor);
/* Lê no máximo 256 bytes por passagem. EOF é persistente. */
bool terminal_poll(struct terminal_input *input, bool *stop_requested,
                   bool *end_of_input);
/* Restaura os atributos e flags originais, inclusive após preparação parcial. */
bool terminal_close(struct terminal_input *input);

#endif
