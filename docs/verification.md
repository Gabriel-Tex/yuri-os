# Registro de verificação

## US01 — Registro e estados do cliente

Data: 2026-10-22. Ambiente: Linux x86_64, kernel `7.0.0-28-generic`, GCC 13.3.0 (`cc`, Ubuntu 13.3.0-6ubuntu2~24.04.1). Código próprio compilado em C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `make clean` | Removeu os artefatos gerados de `build/`; a compilação seguinte partiu sem objetos anteriores. |
| `make test` | Compilação com `-O2 -g`, sem avisos; cinco grupos de testes passaram. |
| `make debug` | Compilação separada com `-O0 -g3`, sem avisos; cinco grupos passaram. |
| `make -n -W src/domain/client.h` | Simulação de mudança no cabeçalho programou recompilação dos dois objetos e nova ligação do teste; não alterou o cabeçalho. |
| `git diff --check` | Sem erros de whitespace nas alterações rastreadas. |

Cobertura determinística de `tests/domain_test.c`:

- Prioridades alta/baixa com X = 1, 4 e 5 ms; alta com X = 5 resulta em 2.500.000 ns. Demanda mantém microssegundos.
- PID não positivo, prioridade inválida, X zero, ponteiros nulos, maior X conversível e valores acima dele; soma com prazo exatamente `UINT64_MAX` e soma que o ultrapassa. Recusas preservam todos os campos.
- Fluxo `STARTING → QUEUED → IN_SERVICE → COMPLETED`, com chegada e prazo preservados; conclusão anterior à chegada recusada, igualdade aceita e prazo vencido aceito. Sequência e demanda nos limites representáveis.
- Matriz das cinco operações de transição nos seis estados, além de estado inválido. Cancelamento em espera, falha nos estados ativos e recusa de transições terminais.
- Repetição de conclusão, cancelamento e falha após cada estado terminal, sem sobrescrever o evento anterior.

Os testes não consultam relógio, criam filhos ou adquirem IPC. R13 está verificado quanto à representação e preservação da chegada fornecida; captura real na criação depende das histórias de processos e Recepção. Satisfação, escalonador, protocolo do Analista e compatibilidade com o cliente oficial ainda não foram implementados nem verificados. A fila é tratada separadamente na US02. Não há medição de satisfação ou tempo total do produto nesta entrega.

## US02 — Fila limitada e seleção EDF

Data: 2026-11-05. Ambiente: Linux x86_64; código próprio compilado em C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `make test` | Teste da US01 (cinco grupos) e teste da fila (quatro grupos) passaram, sem avisos de compilação. |
| `git diff --check` | Sem erros de whitespace nas alterações rastreadas. |

`tests/client_queue_test.c` usa registros e prazos artificiais para verificar fila vazia e unitária; reserva para `STARTING`; recusa de inserção incompleta; limite de 100 considerando a reserva; recusa da 101ª; liberação e reocupação após retirada; liberação da reserva após falha; seleção por prazo e desempate por sequência mesmo com inserção em ordem inversa; cliente de prazo vencido ainda elegível; ponteiros nulos e inserção sem reserva. A retirada devolve um registro `QUEUED`, e o teste o transfere a `IN_SERVICE` fora da fila.

O teste não mede desempenho nem usa relógio real, processos ou IPC. A pausa e a retomada da Recepção ao atingir 100 aguardam a integração dos eventos nas US06/US14; R15 está verificado aqui apenas como limite de capacidade. Não há medição de satisfação ou tempo total do produto nesta história.
