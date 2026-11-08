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

## US03 — Satisfação e métricas

Data: 2026-11-05. Ambiente: Linux x86_64; compilação C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `make test` | US01 (cinco grupos), US02 (quatro) e US03 (quatro) passaram; compilação dos novos módulos sem avisos. |

`tests/metrics_test.c` verifica prioridade alta com X = 5 ms e limite de 2,5 ms, igualdade e ultrapassagem por 1 ns, prioridade baixa com X ms, evento terminal repetido, cancelamento excluído do denominador infinito, finito incompleto ou falho recusado, zero concluídos, saída de taxa inalterada em erro, limites de contadores e conversões de `timespec` e duração, incluindo overflow. Os casos usam somente instantes fornecidos pelo teste; a função que lê o relógio real é compilada, mas não chamada no teste determinístico.

Captura real da chegada, coleta da conclusão, confirmação de status do cliente e medição do tempo total dependem da aplicação e dos processos. Não há medição de satisfação nem de tempo total do produto nesta etapa.

## US04 — Contextos e pilhas

Data: 2026-11-05. Ambiente: Linux x86_64/glibc; código próprio compilado em C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 10s make test` | Os três testes anteriores e `scheduler_test` passaram; os novos fontes compilaram sem avisos. O watchdog não disparou. |

`scheduler_test.c` despacha duas rotinas com pilhas próprias e verifica variáveis locais preservadas após `scheduler_yield`, retorno pelo `uc_link`, término em ambas as ordens e recusa de novo despacho de uma rotina terminada. Um alocador controlado falha na segunda pilha; a destruição libera a primeira uma vez e tolera repetição. Chamadas inválidas também são recusadas.

O teste cobre contextos e liberação após retorno ao escalonador. Ainda não há seleção automática, espera por condição, processos clientes nem carga oficial. Ferramentas de análise de memória e ThreadSanitizer podem não representar corretamente trocas por `ucontext`; não foram executadas aqui. O teste não mede desempenho do serviço.

## US05 — Escalonamento round-robin

Data: 2026-11-06. Ambiente: Linux x86_64/glibc; código próprio compilado em C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 10s make test` | Os quatro testes passaram; o watchdog não disparou. O módulo alterado compilou sem avisos com `-O2 -g`. |
| `timeout 20s make debug` | Os quatro testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |
| `git diff --check` | Sem erros de whitespace nas alterações rastreadas. |

`scheduler_test.c` verifica alternância automática das duas rotinas pela sequência observada, com cada uma terminando primeiro em cenários distintos. Também verifica execução de uma única rotina criada, estado `RUNNING` exclusivo durante cada etapa, recusa de reentrada em `scheduler_run`, ausência de novo despacho após `FINISHED` e repetição de `scheduler_run` sem executar rotinas terminadas. As sequências usam somente cessões explícitas, sem relógio de parede.

O estado `WAITING` ainda não tem transição pública: espera, despertar e processamento de eventos pertencem à US06. Não há clientes, processos ou medição de desempenho do serviço nesta verificação. As limitações de ferramentas de memória para `ucontext` descritas na US04 permanecem.

## US06 — Espera e despertar por condição

Data: 2026-11-06. Ambiente: Linux x86_64/glibc; código próprio compilado em C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 10s make test` | Os quatro testes passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 20s make debug` | Os quatro testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |
| `git diff --check` | Sem erros de whitespace nas alterações rastreadas. |

`scheduler_test.c` usa eventos artificiais para verificar duas rotinas esperando o mesmo evento e acordadas juntas; identificadores distintos; notificação anterior à espera; condição que muda entre duas leituras antes da suspensão; notificação sem condição satisfeita seguida de nova espera; notificação feita por outra rotina pronta; evento processado antes de bloquear; retorno `EAGAIN` quando não há callback de espera; erro do callback de espera e retomada posterior. O callback de processamento é chamado entre os despachos mesmo quando há rotinas prontas. Nenhum caso depende de PIDs, terminal ou tempo de parede.

O callback de espera dos testes produz eventos artificiais e retorna imediatamente após cada um. A integração com `pselect`, máscaras de sinais e eventos reais pertence às histórias de plataforma e aplicação. Não houve medição de satisfação ou tempo total do serviço; as limitações de ferramentas de memória para `ucontext` descritas na US04 permanecem.

## US07 — Criação e coleta de processos

Data: 2026-11-06. Ambiente: Linux x86_64/glibc; código próprio e fixture compilados em C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 20s make test` | Cinco testes passaram com `-O2 -g`, sem avisos. A fixture exibiu os diagnósticos esperados de `execv` e `chdir`; o watchdog não disparou. |
| `timeout 20s make debug` | Cinco testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |
| `timeout 30s bash -c 'for iteration in {1..20}; do ./build/processes_test /home/gabriel/S5/SO-yuri/build/controlled_client >/dev/null 2>&1 || exit 1; done'` | Vinte execuções consecutivas da integração de processos passaram; o watchdog não disparou. |
| `git diff --check` | Sem erros de whitespace nas alterações rastreadas. |

`processes_test.c` usa filhos reais da fixture para confirmar parada inicial, continuação, saída zero, saída com código 7, término por `SIGTERM`, falha de `execv` e diretório inexistente com saída 127. Também verifica máscara de `SIGCHLD` restaurada no filho, máscara adicional preservada, fechamento de descritor herdado, diretório de trabalho, rejeição de PID não positivo, restauração do handler e da máscara no pai, e fechamento repetido. Quatro filhos são observados com `waitid(..., WNOWAIT)` antes de uma única chamada a `processes_collect`, que recolhe os quatro; `waitpid` confirma `ECHILD` ao fim. Um segundo observador exercita `pselect` com `SIGCHLD` originalmente bloqueado. O diretório temporário criado pelo teste é removido.

Falha real de `fork` não foi provocada: o módulo devolve erro sem modificar o PID de saída, mas a condição depende de limites do sistema e não foi alterada no ambiente de teste. Falhas das chamadas de configuração de sinais e o fallback de `close_range` para kernels antigos também não foram injetados. `execv` falho é identificado no pai por saída 127 e diagnóstico do filho; o mesmo código pode ser emitido por um programa executado. O cliente oficial e o serviço completo permanecem fora desta verificação; não há medição de satisfação ou duração do produto.

## US08 — Demanda serializada

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; código próprio e fixture compilados em C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 30s make test` | Seis testes passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 40s make debug` | Seis testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |

`demand_test.c` usa filhos reais e diretório temporário. Verificou que uma segunda criação é recusada enquanto há cliente `STARTING`, inclusive quando o primeiro atrasa a parada; leitura antes da parada e evento de PID alheio são recusados. Após `SIGSTOP` do PID correto, consumiu demandas diferentes em sequência, removeu o arquivo antes da nova criação e recolheu ambos os filhos. Demanda negativa foi recusada sem modificar a saída nem apagar o arquivo; arquivo ausente e saída antes da parada também produziram erro. Um arquivo preexistente foi preservado e impediu a criação. O teste removeu seu próprio diretório e não deixou filhos.

A fixture controlada usa valores conhecidos; ainda não comprova compatibilidade com o cliente oficial. Falhas reais de relógio, `fork`, leitura parcial e remoção do arquivo não foram injetadas. A medição de chegada foi conferida apenas quanto à ordem e ao intervalo monotônico, sem avaliar satisfação ou desempenho do serviço.
