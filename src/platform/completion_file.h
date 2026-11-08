#ifndef COMPLETION_FILE_H
#define COMPLETION_FILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <semaphore.h>
#include <sys/types.h>

#define COMPLETION_FILE_NAME "completed.log"
#define COMPLETION_BATCH_LIMIT 10

struct completion_file {
    int directory_fd;
    int file_fd;
    sem_t *block;
    uint64_t temporary_sequence;
    bool initialized;
    bool locked;
};

/* O diretório e o semáforo são fornecidos pelo chamador. create=true exige
 * arquivo ausente; o Analista abre o arquivo já criado pelo serviço. */
bool completion_file_open(struct completion_file *file, const char *directory,
                          sem_t *block, bool create);
bool completion_file_close(struct completion_file *file);

/* O Atendente usa a tentativa; só o processo Analista pode aguardar. */
bool completion_file_try_lock(struct completion_file *file, bool *acquired);
bool completion_file_lock(struct completion_file *file);
bool completion_file_unlock(struct completion_file *file);

/* Todas as operações exigem o bloqueio. Linhas inválidas são recusadas;
 * consume imprime no máximo dez PIDs e remove apenas linhas confirmadas. */
bool completion_file_append(struct completion_file *file, pid_t pid);
bool completion_file_count(struct completion_file *file, size_t *count);
bool completion_file_print_batch(struct completion_file *file, int output_fd,
                                 size_t *printed);

#endif
