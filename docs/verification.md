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

## US09 — Semáforos e instâncias

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall -Wextra -Wpedantic`; semáforos POSIX nomeados no namespace da máquina.

| Comando | Resultado observado |
|---|---|
| `timeout 30s make test` | Sete testes passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 40s make debug` | Sete testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |
| `timeout 40s bash -c 'for iteration in {1..20}; do ./build/ipc_test >/dev/null || exit 1; done'` | Vinte execuções consecutivas passaram; o watchdog não disparou. |
| `timeout 40s bash -c 'for iteration in {1..20}; do ./build/demand_test /home/gabriel/S5/SO-yuri/build/controlled_client >/dev/null || exit 1; done'` | Vinte execuções consecutivas da US08 passaram; o watchdog não disparou. |

`ipc_test.c` cria os semáforos inicialmente disponíveis, verifica `sem_trywait` ocupado sem espera, acesso por outro handle e exclusão de uma segunda instância em processo filho. Após o fechamento repetido, os dois nomes estavam ausentes. Com `/sem_block` criado pelo próprio teste antes da inicialização, esta falhou com `EEXIST`, removeu apenas `/sem_atend` criado parcialmente e preservou `/sem_block` até o teste liberá-lo. Uma nova inicialização após a limpeza passou. O filho foi recolhido; o arquivo de lock permanece deliberadamente em `/tmp` com o mesmo inode, sem bloqueio ativo.

O teste não modifica semáforos preexistentes para preparar o ambiente: se houver um deles antes da execução, falha no pré-requisito e exige diagnóstico da origem. Falhas reais de `sem_close`, `sem_unlink` e `flock` não foram injetadas. A integração de `/sem_atend` com o cliente oficial e `/sem_block` com o Analista ainda depende das histórias seguintes; não houve medição de satisfação nem duração do serviço.

## US10 — Arquivo de PIDs

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 40s make test` | Oito testes passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 50s make debug` | Oito testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |

`completion_file_test.c` usou diretório temporário e semáforo POSIX sem nome. Verificou bloqueio ocupado sem espera, arquivo vazio, inclusão e saída em ordem para 1, 10, 11 e 27 PIDs, limites dos lotes e contagem após cada compactação. A saída `/dev/full` falhou com `ENOSPC` e preservou a linha não impressa; uma linha inválida foi recusada sem alterar a saída nem o arquivo. Dois handles abertos antes da compactação observaram a inclusão posterior no inode novo. O teste removeu seus arquivos e semáforo.

Falhas reais de escrita parcial e de `renameat` não foram injetadas; o código trata escritas parciais e constrói o arquivo substituto antes da troca. A confirmação pelo Analista, a coordenação dos despertares e o cliente oficial ainda não foram verificados nesta história. Não há medição de satisfação ou tempo do serviço.

## US11 — Processo Analista

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 40s make test` | Nove testes passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 50s make debug` | Nove testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |

`analyst_test.c` criou o Analista como filho real e confirmou `analyst.pid`, parada inicial sem saída, um lote por `SIGCONT` e nova parada. Foram exercitados lotes de 0, 1, 10, 11 e 27 PIDs; a saída e a contagem restante corresponderam a cada despertar. O teste encerrou e recolheu o primeiro Analista. Um segundo Analista com `stdout` em `/dev/full` terminou com erro e preservou o PID pendente. Um terceiro encontrou linha inválida, terminou com erro e deixou o arquivo intacto. Os diagnósticos dessas duas falhas são esperados. O teste removeu os arquivos temporários e os semáforos próprios e confirmou ausência de filhos.

Os PIDs usados como conteúdo são sintéticos; a criação e o recolhimento do Analista são reais. A política de despertar e a drenagem automática do serviço pertencem à US12 e à integração posterior. O cliente oficial e as métricas do serviço ainda não foram exercitados.

## US12 — Coordenação e drenagem

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 40s make test` | Dez testes passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 50s make debug` | Dez testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |
| `timeout 60s bash -c 'make -s build/analyst_coordinator_test && for iteration in {1..20}; do ./build/analyst_coordinator_test /home/gabriel/S5/SO-yuri/build/analyst >/dev/null || exit 1; done'` | Vinte execuções consecutivas passaram; o watchdog não disparou. |

`analyst_coordinator_test.c` usou o Analista real, um observador de processos e duas threads de usuário. A drenagem começou com 27 PIDs sintéticos e confirmou lotes de 10, 10 e 7; um PID recebido durante lote ativo aguardou o evento do runtime e formou um último lote de 1. Um segundo `SIGCONT` enquanto o Analista estava ativo não foi enviado. A Recepção progrediu nesse intervalo, sem aguardar o arquivo. A conclusão exigiu arquivo vazio e parada confirmada. O teste recolheu o primeiro Analista e verificou saída ordenada de 28 PIDs; um segundo Analista imprimiu um lote parcial por ociosidade, recusou um despertar repetido durante o lote e, ao morrer por sinal em outro lote, foi reconhecido como erro. O `SIGKILL` durante a compactação deixou um arquivo temporário em uma repetição; após recolher o filho, o teste removeu esse artefato do seu diretório isolado. Não restaram filhos ou semáforos próprios.

A coordenação foi exercitada como módulo de aplicação isolado. A geração de clientes, o atendimento completo, o comando `s` e a integração com o cliente oficial pertencem às histórias seguintes. Não houve medição de satisfação ou duração do serviço.

## US13 — Entrada e preparação

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 50s make test` | Testes existentes, `service_test` e `service_cli_test.sh` passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 55s make debug` | Os mesmos testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |

`service_cli_test.sh` rejeitou falta de argumentos, extras, sinal, zero em X, sufixos e overflow. Uma configuração finita e N = 0 foram reconhecidas a partir de outro diretório por um caminho com espaços. Ambas retornaram código 2 após preparar os recursos, sem alegar atendimento concluído. `service_test.c` preparou um diretório com espaços, confirmou o Analista parado e duas rotinas com contextos distintos, depois recolheu o filho e liberou semáforos, arquivo e diretório próprios. Falha de `exec` e diretório `run` ocupado por arquivo foram recusados sem filhos remanescentes.

A primeira execução de `service_test` revelou estado do Analista não reinicializado após o fechamento; o descritor foi corrigido e a suíte repetida passou. A CLI ainda não gera nem atende clientes: a execução finita e o laço contínuo N = 0 dependem de US14–US18. Portanto não há relatório, taxa ou tempo total do produto nesta verificação. O cliente oficial permanece indisponível.

## US14 — Geração na Recepção

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall -Wextra -Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 80s make test` | Doze testes C e o teste da CLI passaram com `-O2 -g`, sem avisos; o watchdog não disparou. |
| `timeout 85s make debug` | Os mesmos testes passaram com `-O0 -g3`, sem avisos; o watchdog não disparou. |
| `timeout 90s bash -c 'for iteration in {1..5}; do ./build/reception_test /home/gabriel/S5/SO-yuri/build/analyst /home/gabriel/S5/SO-yuri/build/controlled_client >/dev/null || exit 1; done'` | Cinco execuções consecutivas passaram; o watchdog não disparou. |

`reception_test.c` usou o cliente controlado e um Atendente de teste que retira e cancela um cliente por vez, sem simular sucesso de atendimento. A Recepção criou exatamente 1 e 125 clientes nos cenários finitos. Em N = 0, continuou até a parada controlada após pelo menos 205 criações. Com a fila cheia, a geração aguardou a liberação de uma vaga; a ocupação nunca ultrapassou 100, incluindo a reserva de `STARTING`. A Recepção registrou criação antes da fila, observou a parada inicial, leu a demanda e enfileirou apenas registros completos; o teste conferiu demanda em microssegundos e paciência de 2,5/5 ms para X = 5. A função de mapeamento da prioridade foi conferida em 256 padrões de bits (128 para cada classe), sem exigir divisão exata numa amostra aleatória.

Uma demanda inválida produziu erro sem inserção na fila. A limpeza cancelou e recolheu o filho parado e liberou a reserva. `demanda.txt` permaneceu disponível até o fechamento controlado do serviço, que agora o remove junto ao diretório próprio. Nenhum filho ou semáforo criado pelo teste permaneceu. O cliente oficial não foi usado; a CLI do produto ainda não ligava a Recepção ao Atendente real nesta etapa. Não houve medição de satisfação ou duração do serviço, nem alegação de ganho de desempenho.

## US15 — Atendimento confirmado

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall`, `-Wextra` e `-Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 90s make test` | Treze testes C e teste da CLI passaram com `-O2 -g`, sem avisos; watchdog não disparou. |
| `timeout 100s make debug` | Os mesmos testes passaram com `-O0 -g3`, sem avisos; watchdog não disparou. |

`attendant_test.c` inseriu um cliente já parado diretamente na fila, com prazo vencido. A fixture atrasou 30 ms antes de adquirir `/sem_atend` e saiu normalmente após o serviço; o Atendente esperou a saída, registrou um concluído e zero satisfeitos, o Analista imprimiu o PID e a drenagem confirmou arquivo vazio. Outra fixture terminou por sinal após `SIGCONT`; não houve conclusão nem PID registrado, e a limpeza marcou uma falha e recolheu o filho. O teste também verificou ausência de filhos remanescentes. O estado ativo do Analista e a preservação de PIDs durante seu lote foram exercitados no teste real da US12; a integração com geração simultânea e relatório é verificada na US16. O cliente oficial permanece indisponível.

## US16 — Execução finita e relatório

Data: 2026-11-08. Ambiente: Linux x86_64/glibc; C17 com `-Wall`, `-Wextra` e `-Wpedantic`.

| Comando | Resultado observado |
|---|---|
| `timeout 100s make test` | Quatorze testes C e dois testes da CLI passaram com `-O2 -g`, sem avisos; watchdog não disparou. |
| `timeout 110s make debug` | Os mesmos testes passaram com `-O0 -g3`, sem avisos; watchdog não disparou. |
| `timeout 90s bash -c 'for iteration in {1..10}; do bash ./tests/finite_service_test.sh /home/gabriel/S5/SO-yuri/build/service /home/gabriel/S5/SO-yuri/build/controlled_client >/dev/null || exit 1; done'` | Dez repetições dos três tamanhos passaram; watchdog não disparou. |

`finite_service_test.sh` iniciou a CLI a partir de `/tmp`, indicando explicitamente a fixture por `SERVICE_CLIENT_PATH`, para N = 1, 11 e 125, com X = 10000 ms. Em cada execução, contou N linhas de PID distintas antes do relatório, confirmou criados = concluídos = N e satisfeitos + insatisfeitos = N, fração da taxa coerente e tempo total positivo. N = 11 e N = 125 exigiram drenagem do lote parcial; N = 125 passou pelo limite da fila. `finite_service_test.c` executou o fluxo de 11 clientes dentro do processo de teste, confirmou resultado completo, ausência de filhos após o fechamento e remoção de `/sem_atend` e `/sem_block`. Uma falha de `exec` após o `fork` foi recusada sem relatório completo nem filho remanescente. A suíte também manteve os testes de saída por sinal e demanda inválida. Os valores de satisfação e duração dependem da carga e não foram usados como comparação de desempenho. O cliente oficial permanece indisponível, portanto sua compatibilidade não foi verificada.

## US17 — Entrada e restauração do terminal

Em 2026-11-09, `make build/terminal_test && timeout 10s ./build/terminal_test` passou. O teste cobriu pipe, pseudoterminal, tecla diferente de `s`, `s` sem Enter, EOF persistente e restauração repetida de flags e atributos. `timeout 120s make test` passou, incluindo os cenários finitos existentes. A ligação do adaptador à execução infinita será verificada na US18.

## US18 — Parada e drenagem do modo infinito

Em 2026-11-09, `timeout 120s make test` passou com C17, `-Wall -Wextra -Wpedantic`. `infinite_service_test.sh` enviou `s` imediatamente e depois de outra tecla com carga em andamento, conferiu PIDs impressos iguais às conclusões, denominador concluídos, tempo positivo e EOF como erro sem relatório de sucesso. `infinite_service_test.c` enviou `s` após um segundo de carga, observou conclusões e cancelamentos, confirmou criados = concluídos + cancelados e verificou ausência de filhos e dos dois semáforos após `service_close`. Cinco execuções adicionais com `s` após 0,7 s passaram sob watchdog externo de 15 s. A execução com a fixture não verifica a compatibilidade com o cliente oficial, ainda indisponível. As taxas e durações observadas não foram usadas como comparação de desempenho.

## US19 — Erros e limpeza controlada

Em 2026-11-09, `timeout 120s make test` e `timeout 120s make debug` passaram sem avisos de compilação. `infinite_service_test.c` enviou `SIGINT` e `SIGTERM` ao processo de serviço durante carga, confirmou resultado incompleto, fechamento, ausência de filhos e semáforos e restauração dos handlers e da máscara. `finite_service_test.c` exercitou `exec` inválido, demanda ausente, término anormal do cliente e morte do Analista; após cada erro, verificou que não havia filhos ou semáforos. `error_service_test.sh` conferiu status não zero e diagnóstico da CLI para demanda ausente, cliente anormal e saída do Analista em `/dev/full`, sem taxa de sucesso. O watchdog externo não disparou. Falha real de `fork` e falhas de cada chamada de liberação não foram injetadas; os adaptadores verificam seus retornos, mas não há comprovação empírica desses caminhos. A fonte oficial do cliente segue indisponível.

## US20 — Construção e execução

Em 2026-11-09, em uma cópia isolada das fontes, `make clean` seguido de `timeout 120s make -j4 test` compilou o código próprio em C17 com `-Wall -Wextra -Wpedantic` e passou sem avisos. `make -n -W src/domain/client.h build/service` indicou recompilação de `client.c` e `service.c`. `ldd` em `build/service` e `build/analyst` listou a libc e os componentes do carregador Linux.

`make client` retornou erro com indicação de `provided/client.c` quando a fonte estava ausente. Na mesma cópia, `make clean` removeu `build/` e preservou o marcador `run/keep` e `src/service_main.c`. Depois, `make client` compilou a listagem extraída da página 3 do PDF, sem editar o código. A verificação do protocolo e da execução com essa fonte pertence à US21.

## US21 — Cliente do enunciado

Em 2026-11-09, a página 3 do PDF foi conferida visualmente e confirmada pelo usuário como fonte oficial da entrega. O PDF do ZIP de estudos tem o mesmo SHA-256 do arquivo no repositório e não contém anexos. A origem e o hash da transcrição estão em [provided/README.md](../provided/README.md). A integração existente aceitou a listagem sem adaptações.

| Comando | Resultado observado |
|---|---|
| `timeout 120s make -j4 test` | Suíte controlada passou sem avisos de compilação. |
| `timeout 90s make test-client` | Finito com N = 1, 11 e 125, X = 10000 ms, e infinito com `s` imediato, `s` após 0,7 s e EOF. A CLI encontrou o cliente irmão com `SERVICE_CLIENT_PATH` desativado. PIDs únicos, totais e denominadores conferidos. |
| `timeout 20s ./build/infinite_service_test "$PWD/build/analyst" "$PWD/build/client"` | Com `s` houve conclusões e cancelamentos. O teste verificou também SIGINT e SIGTERM, ausência de filhos por `ECHILD`, restauração das máscaras e remoção de ambos os semáforos. |

Uma execução finita com N = 11 e X = 10000 ms sob `strace -ff` confirmou a escrita decimal sem quebra de linha, a parada inicial, a abertura de `/sem_atend` após a retomada e a espera do cliente. O Analista gravou `analyst.pid`, parou e imprimiu lotes de 1 e 10. A ordem dos 11 PIDs impressos coincidiu com a ordem das saídas zero recolhidas pelo pai. A execução terminou com `ECHILD` e sem diretório de execução ou semáforos remanescentes. O rastreamento exigiu execução fora da sandbox devido a `ptrace` e não compõe as medições de desempenho. A saturação e a retomada da fila de 100 foram verificadas de forma controlada na US14.

## US22 — Avaliação e rastreabilidade

Em 2026-11-09, `timeout 120s make test`, `timeout 120s make debug` e `timeout 90s make test-client` passaram em sequência sem avisos de compilação. O roteiro de medição recusou N = 0 com status 2. O teste funcional do modo infinito continua em `make test` e `make test-client`, incluindo a comparação da saída do Analista com o denominador do relatório e a restauração de recursos. Após os testes, `run/` não continha diretórios `execution-*` e `/dev/shm` não continha `sem.sem_atend` ou `sem.sem_block`.

| Requisito | Evidência executada |
|---|---|
| R01 | Build e `ldd` em Linux (US20). Execução com processos e IPC reais (US21). Busca em `src/` sem cabeçalho ou chamadas `pthread`. |
| R02 | `scheduler_test` preserva pilhas e contextos distintos, verifica round-robin e espera por evento (US04–US06). |
| R03 | CLI recusa argumentos inválidos e executa N = 1, 11, 125 e 0 (US13, US21). |
| R04 | `reception_test` verifica mapeamento equiprovável dos bits usados no sorteio (US14). A proporção observada em uma execução não precisa ser exatamente 50/50. |
| R05 | `domain_test` e `metrics_test` cobrem X par e ímpar, inclusive 2,5 ms para alta com X = 5 (US01, US03). |
| R06 | `metrics_test` cobre igualdade e ultrapassagem de 1 ns. `attendant_test` usa cliente retomado após o prazo e espera seu término (US03, US15). |
| R07 | Finito com os dois clientes valida N criações, N conclusões, N PIDs e taxa satisfeitos/N. Saídas anormais não contam como sucesso (US16, US19, US21). |
| R08 | Infinito valida `s` com zero e com conclusões. Teste direto confirma cancelamentos excluídos e a CLI confere o denominador (US18, US21). |
| R09 | Rastreamento de 11 clientes confirma os PIDs impressos na ordem das saídas zero recolhidas (US21). |
| R10 | `analyst_test` e rastreamento verificam parada inicial e retomada autorizada (US11, US21). |
| R11 | Teste de arquivo e Analista verifica ordem e resto. Coordenador drena 27 registros em lotes de 10, 10 e 7 (US10–US12). |
| R12 | Coordenador exige arquivo vazio e parada confirmada. Testes infinitos conferem todos os PIDs concluídos antes do relatório (US12, US18, US21). |
| R13 | Testes de domínio, demanda e Recepção verificam os campos e a chegada registrada na criação (US01, US08, US14). |
| R14 | `demand_test` recusa outra inicialização antes do consumo e valida demandas distintas. O cliente do PDF escreve e para na sequência esperada (US08, US21). |
| R15 | `reception_test` atinge 100, verifica pausa e retomada após liberar vaga (US14). |
| R16 | `ipc_test` verifica semáforos e propriedade. O Analista cria e o coordenador verifica `analyst.pid`. Testes e rastreamento conferem uso e limpeza (US09, US11–US12, US19, US21). |
| R17 | Transcrição conferida com o PDF, SHA-256 registrado e execução dos dois modos (US21). |
| R18 | Conversão monotônica testada. O relatório finito e infinito contém tempo positivo após drenagem e limpeza (US03, US16, US18). |

### Medições finitas

Código do serviço e dos clientes na revisão `a31c421`, compilado com GCC 13.3.0, C17 e `-O2 -g`. Ambiente Linux x86_64 7.0.0-42-generic, glibc 2.39 e Intel Core i5-7200U com quatro CPUs lógicas acessíveis. A carga média de um minuto antes das séries era 2,08. Foi usado o escalonador cooperativo round-robin e EDF não preemptivo com desempate por criação e capacidade de 100 esperas. Cada série executou cinco vezes em sequência, com stdout em arquivo temporário e sem `strace`. O tempo do relatório inclui inicialização, execução, drenagem e limpeza.

```sh
for client in controlled official; do
    for count in 11 125; do
        timeout 240s bash tests/benchmark.sh "$client" "$count" 5 5 || exit 1
    done
done
```

X = 5 ms em todas as séries. Os resultados individuais estão na ordem da execução. Em todas as repetições, criados = concluídos = N e o Analista imprimiu N PIDs distintos antes do relatório. As verificações de semáforos e diretórios de execução passaram após cada série.

| Carga | N | Satisfeitos por repetição | Tempo total por repetição (ms) | Mediana da taxa | Tempo mín. / mediana / máx. (ms) |
|---|---:|---|---|---:|---|
| Controlada | 11 | 4, 1, 2, 1, 1 | 332.221 · 825.998 · 258.576 · 406.293 · 175.851 | 9,09% | 175.851 / 332.221 / 825.998 |
| Controlada | 125 | 3, 3, 5, 4, 2 | 1046.700 · 943.242 · 990.194 · 1076.624 · 1165.255 | 2,40% | 943.242 / 1046.700 / 1165.255 |
| Enunciado | 11 | 2, 2, 2, 1, 2 | 232.284 · 219.154 · 396.500 · 230.815 · 281.996 | 18,18% | 219.154 / 232.284 / 396.500 |
| Enunciado | 125 | 2, 2, 2, 1, 2 | 891.813 · 946.166 · 1130.187 · 1375.164 · 964.749 | 1,60% | 891.813 / 964.749 / 1375.164 |

A carga controlada solicita 1000 µs fixos. A do enunciado solicita 1, 5 ou 15 µs e pode repetir demandas dentro do mesmo segundo. As séries documentam as duas cargas separadamente. Com N = 125 e X = 5 ms, a taxa do cliente do enunciado variou de 0,80% a 1,60%. A medição inclui o custo de criar processos, esperar pelo serviço e processar lotes. O rastreamento da US21 mostrou um `execve` por cliente e uma troca de arquivo por lote, mas não isolou o custo de cada etapa. O gargalo dominante e o efeito de mudanças de política permanecem sem medição.

## Auditoria de conformidade após o épico 7

Em 2026-11-09, revisão `a5ba4dc`. A auditoria cruzou o texto da página 1, o diagrama e as notas da página 2, a listagem da página 3, o código e os testes do repositório. `timeout -k 5s 120s make test` e `timeout -k 5s 90s make test-client` passaram. Ao final das verificações da CLI, `run/` não continha diretórios `execution-*` e os dois semáforos nomeados estavam ausentes. As verificações abaixo delimitam a cobertura das suítes. [Histórias de correção e fechamento](user-stories.md#e08--conformidade-e-fechamento-da-primeira-parte).

| Pendência | Evidência atual |
|---|---|
| US23 — Protocolo do diagrama | A página 2 atribui a criação de `sem_atend` e `sem_block` à Recepção, a espera por `sem_atend` ao Atendente e os despertares para impressão ao Atendente. Em `service_prepare`, `ipc_service_init` cria os semáforos antes do escalonamento. `workers_attendant_run` aguarda a saída do filho e só opera sobre o semáforo do arquivo por meio do coordenador. `service_process_events` pode despertar o Analista durante a drenagem depois que as duas threads terminaram. Os testes da US21 comprovam o uso de `sem_atend` pelo cliente, os lotes e a ordem dos PIDs, sem comprovar essas atribuições no serviço. |
| US24 — Coleta em falha | Um programa temporário colocou o PID de um filho terminado na fila e entregou seu evento pelo callback normal da Recepção. O Atendente sinalizou `EPROTO`, `waitpid` confirmou `ECHILD` e o registro ainda estava na fila. A chamada a `workers_reception_close` excedeu o watchdog de 2 s, com status 124. O cenário isolado não abriu IPC nem deixou um filho por recolher. A inspeção também mostrou coletores de limpeza que ignoram eventos fora de seu subconjunto. |
| US25 — Entrada contínua | Um programa temporário chamou `terminal_poll` com arquivo de 16 MiB cujo primeiro byte era `s`. A função retornou `stop=1`, mas só depois de chegar ao byte 16777216 e a EOF. Com `/dev/zero`, a mesma chamada excedeu o watchdog de 2 s, com status 124. O teste isolou o leitor e não avaliou a latência da CLI com um produtor de pipe. A suíte existente usa `s` imediato e após 0,7 s, sem sincronização com cada fase. |
| US26 — Limite de X | `timeout -k 3s 10s env -u SERVICE_CLIENT_PATH ./build/service 1 18446744073709` passou na primeira tentativa e retornou status 1 na segunda, com `criados=1`, `concluídos=0` e `No such file or directory`. O valor passa no parser e deixa apenas 551615 ns para a chegada de um cliente de baixa prioridade antes de estourar a soma do prazo. O erro do domínio não define `errno`, o que explica o diagnóstico não relacionado. |
| US26 — Origem da medição | A nota 4 mede a chegada entre o início do programa e a criação do cliente. `main` valida argumentos e resolve os executáveis antes de `service_prepare` capturar a origem de `start_ns`. Essa constatação vem da inspeção do código. A auditoria não quantificou a duração omitida. |

As medições da US22 continuam válidas para a revisão e a janela que foram usadas. A satisfação baixa observada com X = 5 ms faz parte da referência de desempenho. O enunciado não define uma taxa mínima nem um tempo máximo como requisito de aceite. A US27 exige uma referência na revisão final após os ajustes de conformidade.

## US23 — Protocolo de sincronização do diagrama

Em 2026-11-10, a Recepção passou a criar `/sem_atend` e `/sem_block` antes de iniciar o Analista ou criar clientes. O Atendente passou a exigir saída zero e disponibilidade posterior de `/sem_atend` antes de registrar conclusão; o pedido de drenagem é feito por ele antes de terminar. `attendant_test` usa uma fixture que demora antes de adquirir o semáforo, outra que morre por sinal e outra que sai com status zero deixando o semáforo ocupado. Esta última não entra nas conclusões. O teste de Recepção cobre geração finita, contínua e limite de 100; os testes de arquivo e coordenador cobrem lotes parciais e 27 PIDs em 10, 10 e 7.

| Comando | Resultado observado |
|---|---|
| `timeout -k 5s 120s make test` | Passou, inclusive os cenários novos do Atendente, sem avisos de compilação. |
| `timeout -k 5s 120s make debug` | Passou com `-O0 -g3`, sem avisos de compilação. |
| `timeout -k 5s 90s make test-client` | Passou com o cliente da página 3: N = 1, 11, 125 e infinito com `s`. |

Os testes funcionais não são medições comparativas de desempenho. A US24 ainda trata a propriedade de eventos terminais agrupados durante falhas.

## US24 — Propriedade e coleta de filhos

Em 2026-11-10, a Recepção passou a reconhecer saídas terminais de PIDs ainda enfileirados, e os callbacks de limpeza passaram a registrar o recolhimento também para filhos de outro proprietário. `ownership_test` matou clientes em inicialização, na fila e em atendimento, reuniu eventos terminais em uma coleta e verificou que os três foram contabilizados como falhos sem conclusão. Em outro caso, a limpeza do Atendente consumiu eventos dos clientes da Recepção e ela terminou sem esperar um segundo evento. O teste também herdou `SIGTERM` bloqueado, encerrou o Analista, verificou `ECHILD`, ausência de ambos os semáforos e preservação da máscara original. O cenário de propriedade foi repetido dez vezes sob watchdog.

| Comando | Resultado observado |
|---|---|
| `timeout -k 2s 30s bash -c 'for iteration in {1..10}; do ./build/ownership_test "$PWD/build/analyst" "$PWD/build/controlled_client" >/dev/null || exit 1; done'` | Dez repetições passaram; watchdog não disparou. |
| `timeout -k 5s 120s make test` | Suíte completa passou, incluindo o novo teste e os modos finito e infinito. |
| `timeout -k 5s 120s make debug` | Suíte completa passou com `-O0 -g3`. |
| `timeout -k 5s 90s make test-client` | Cliente da página 3 passou em N = 1, 11, 125 e infinito com `s`. |

O SHA-256 de `provided/client.c` continuou `915feefbe60d63bb32fc1d348eefefadb740dfefff53211c0d3319b641f63ced`. Depois das suítes, os nomes `/sem_atend` e `/sem_block` estavam ausentes. A verificação de US24 não mede desempenho; as séries da US22 referem-se à revisão anterior e não são comparações de ganho.

## US25 — Parada sob carga e em cada fase

Em 2026-11-10, `terminal_poll` passou a ler no máximo 256 bytes por passagem. `terminal_test` verificou o deslocamento do arquivo após cada leitura e o reconhecimento de `s` no terceiro bloco. A CLI recebeu 65.536 bytes antes de `s` com um produtor contínuo depois dele e terminou com relatório coerente. `stop_phase_test` enviou `s` antes do primeiro cliente, durante a escrita atrasada da demanda, durante um atendimento de dois segundos, com 100 vagas ocupadas, durante um lote do Analista mantido ativo por fixture e em pseudoterminal sem Enter. O teste conferiu conclusão do atendimento em curso, cancelamento dos demais, ausência de filhos e semáforos, diretório removido e restauração de `termios`. `reception_test` continuou cobrindo a retomada da geração após abrir vaga na fila cheia.

| Comando | Resultado observado |
|---|---|
| `timeout -k 5s 120s make test` | Passou com os testes de fase e a CLI de entrada contínua. |
| `timeout -k 5s 120s make debug` | Passou com `-O0 -g3`. |
| `timeout -k 5s 90s make test-client` | Passou com o cliente do enunciado nos modos finito e infinito. |
| `timeout -k 3s 40s bash -c 'for iteration in 1 2 3; do ./build/stop_phase_test "$PWD/build/analyst" "$PWD/build/slow_analyst" "$PWD/build/controlled_client" >/dev/null || exit 1; done'` | Três repetições passaram; watchdog não disparou. |

Os testes de fase usam o cliente e o Analista controlados para sincronizar a parada. A suíte da CLI confere PIDs impressos iguais às conclusões, denominador sem cancelados e taxa não aplicável com zero conclusões. Não foi feita comparação de desempenho.

## US26 — Limites de X e origem da medição

Em 2026-11-10, CLI e domínio passaram a usar `CLIENT_MAX_PATIENCE_MS = 18.446.744.073.709` ms. O prazo absoluto usa um bit de transporte e a parte baixa em nanossegundos; assim, toda chegada representável produz prazo válido para X aceito. `domain_test` exercitou os dois tipos de prioridade com X máximo, X ímpar, soma exata em `UINT64_MAX` e transporte com chegada um nanossegundo maior. `client_queue_test` verificou a ordem EDF entre prazos antes e depois desse limite. Os testes de métricas continuam cobrindo resposta igual à paciência e um nanossegundo acima.

`service_cli_test.sh` executou N = 1 com o maior X suportado e recusou o primeiro valor acima dele, `18.446.744.073.710`, com diagnóstico de faixa. `service_test` forneceu uma origem um segundo antes da preparação, confirmou sua preservação e duração final de pelo menos um segundo; também recusou origem futura antes de criar recursos. A inspeção de `service_main.c` confirmou que a amostra inicial ocorre antes da validação e da resolução de caminhos. A CLI calcula a duração final depois de `service_close` e de liberar os caminhos alocados. A chegada continua sendo amostrada junto à criação, antes do `fork`, e a conclusão na saída zero observada.

| Comando | Resultado observado |
|---|---|
| `timeout -k 5s 120s make test` | Passou, incluindo X máximo e primeiro rejeitado pela CLI. |
| `timeout -k 5s 120s make debug` | Passou com `-O0 -g3`. |
| `timeout -k 5s 90s make test-client` | Passou com o cliente do enunciado nos modos finito e infinito. |
| `timeout -k 3s 30s bash -c 'for iteration in {1..10}; do SERVICE_CLIENT_PATH="$PWD/build/controlled_client" ./build/service 1 18446744073709 >/dev/null || exit 1; done'` | Dez execuções com X máximo passaram, sem diretórios de execução remanescentes. |

O cliente fornecido permaneceu sem alteração. Estes testes comprovam correção e compatibilidade nas condições executadas; não constituem medição comparativa de satisfação ou tempo.
