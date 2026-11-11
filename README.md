# Simulador de atendimento de clientes

Implementação da **primeira parte** do trabalho prático de Sistemas Operacionais. O programa cria processos clientes, organiza sua espera e mede quantos terminaram dentro do prazo. A Recepção e o Atendente são threads de usuário com contextos próprios, executadas por um escalonador do projeto; o Analista é outro processo e imprime os PIDs dos atendimentos concluídos.

O projeto exercita troca de contexto, escalonamento, processos, sinais, semáforos POSIX e comunicação por arquivos. A [arquitetura](docs/architecture.md) distingue os requisitos do enunciado das decisões adotadas aqui. A fonte do cliente da entrega e sua proveniência estão em [provided/](provided/README.md).

## Início rápido

É necessário Linux com glibc, um compilador C17 e GNU Make. Para executar os testes, também são usados Bash, `awk` e `timeout` do GNU Coreutils. Na raiz do repositório:

```sh
make
./build/service 11 10000
```

`make` produz `build/service`, `build/analyst`, `build/client` e os executáveis de teste. O comando acima cria e atende 11 clientes com paciência máxima de 10.000 ms. Os PIDs impressos pelo Analista aparecem antes do relatório final. Execute apenas uma instância do serviço por vez: os semáforos nomeados são compartilhados no sistema.

## Uso

```text
./build/service N X
```

| Argumento | Significado |
|---|---|
| `N` | Inteiro decimal de 0 a 18.446.744.073.709.551.615. Com `N > 0`, cria e atende exatamente N clientes; com `N = 0`, gera clientes até receber `s`. |
| `X` | Paciência máxima em milissegundos: inteiro decimal de 1 a 18.446.744.073.709. |

Sinais, frações, sufixos e valores fora desses limites são rejeitados. O executável procura `analyst` e `client` no mesmo diretório que `service`, mesmo quando o comando é chamado de outro diretório. Para testes com outro cliente, `SERVICE_CLIENT_PATH` pode indicar o caminho **absoluto** de um executável compatível; sem essa variável, é usado `build/client`.

### Modo finito

```sh
./build/service 125 10000
```

A fila comporta até 100 clientes esperando, mas isso não limita o total de uma execução finita: a Recepção aguarda vagas e continua até criar os 125. O serviço termina depois de atender todos, drenar os PIDs pendentes no Analista e liberar seus recursos.

### Modo infinito

```sh
./build/service 0 10000
```

Digite `s` para parar. Em um terminal, não é necessário pressionar Enter; a entrada também pode vir de um pipe. Ao receber o comando, o serviço interrompe novas criações e novos atendimentos, deixa o atendimento em curso terminar, cancela os clientes restantes em espera e drena os PIDs já concluídos. Se a fila chegar a 100 esperas, a geração pausa até surgir uma vaga. Fim da entrada sem `s` é tratado como erro.

## Como funciona

1. **Recepção:** sorteia a prioridade de cada cliente, alta ou baixa, com probabilidade de 50% para cada classe. Reserva uma vaga e registra a chegada junto à criação do processo.
2. **Inicialização do cliente:** o cliente escreve sua demanda em `demanda.txt` e para com `SIGSTOP`. A Recepção confirma essa parada, lê e remove o arquivo, e só então pode criar outro cliente. Assim, demandas de processos diferentes não se misturam.
3. **Fila e Atendente:** a fila guarda até 100 clientes. O Atendente escolhe o menor prazo absoluto de conclusão, com desempate pela ordem de criação (**EDF não preemptivo**). Há um atendimento por vez; o cliente escolhido volta a executar com `SIGCONT`.
4. **Confirmação:** a conclusão só é contabilizada após a saída normal do cliente com status zero e a disponibilidade de `/sem_atend`. O PID é então acrescentado a `completed.log`.
5. **Analista:** começa parado e desperta por solicitação do Atendente. Sob proteção de `/sem_block`, imprime no máximo os dez primeiros PIDs pendentes por despertar, remove apenas os impressos e volta a parar. No encerramento, o serviço repete os despertares até confirmar o arquivo vazio.

Recepção e Atendente têm pilhas independentes e compartilham uma thread do kernel. O escalonador alterna as rotinas prontas em round-robin e as suspende enquanto aguardam eventos; clientes e Analista são processos reais. Os arquivos temporários de cada execução ficam em `run/execution-*/` e são removidos na saída controlada. Os detalhes de estados, propriedade dos recursos e tratamento de falhas constam na [arquitetura](docs/architecture.md#execução-e-sincronização).

### Tempo, prioridade e satisfação

O registro de cada cliente contém PID, instante de chegada, prioridade e demanda. Prioridade baixa recebe paciência `X` ms; prioridade alta recebe `X/2` ms, inclusive a fração quando `X` é ímpar. O sorteio é independente por cliente: não há garantia de metade exata em uma execução.

O tempo de resposta vai da chegada, registrada junto à criação do processo, à conclusão observada pelo serviço. **Espera, inicialização e atendimento entram nesse intervalo.** Um cliente é satisfeito quando `conclusão − chegada ≤ paciência`; igualdade com o prazo conta como satisfação. Um prazo vencido não elimina o atendimento no modo finito.

O cliente fornecido usa sua demanda como argumento de `usleep` em **microssegundos**. `X` e a paciência continuam em **milissegundos**; o projeto não altera a carga, a distribuição nem a temporização dessa fonte. A fonte atual é a transcrição confirmada da página 3 do enunciado. Se o professor fornecer outra versão, registre sua origem e versão em [provided/README.md](provided/README.md) e preserve seu comportamento.

## Relatório e falhas

Após as linhas de PID do Analista, uma execução bem-sucedida mostra `criados`, `concluídos`, `satisfeitos`, `insatisfeitos`, `taxa de satisfação` e `tempo total`. Insatisfeitos são os concluídos que ultrapassaram o prazo.

| Modo | Cálculo da taxa |
|---|---|
| Finito (`N > 0`) | `satisfeitos / N`, depois que todos os N clientes foram atendidos. |
| Infinito (`N = 0`) | `satisfeitos / concluídos`; clientes cancelados em espera ficam fora do denominador. |

Sem conclusões, o relatório informa `taxa de satisfação: não aplicável`. O tempo total, em milissegundos, usa relógio monotônico desde a entrada do programa até depois da drenagem e da limpeza, antes da impressão do relatório.

Argumentos inválidos, falhas de cliente ou Analista, erros de I/O, `SIGINT` e `SIGTERM` resultam em status de erro e diagnóstico em `stderr`. Quando há execução parcial, seus contadores são identificados como parciais; não são apresentados como um relatório de sucesso. Na saída controlada, o serviço recolhe seus processos filhos, libera os recursos que criou e restaura o estado do terminal.

## Construção, testes e medições

| Comando | Finalidade |
|---|---|
| `make` | Compila o serviço, o Analista, o cliente fornecido e os testes. |
| `make client` | Compila `provided/client.c` em `build/client`. |
| `make test` | Executa testes de domínio, escalonador, processos, IPC e integração com cliente controlado. |
| `make test-client` | Exercita os modos finito e infinito com o cliente fornecido. |
| `make debug` | Recompila em `build/debug/` com `-O0 -g3` e executa a suíte. |
| `make clean` | Remove `build/`; preserva fontes e `run/`. |

O código próprio é compilado com C17, `-Wall`, `-Wextra` e `-Wpedantic`. `CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` e `LDLIBS` podem ser passados ao Make; `CLIENT_CFLAGS` controla separadamente a compilação da fonte fornecida. Os testes controlados usam `build/controlled_client` para exercitar estados e falhas difíceis de reproduzir com o cliente do enunciado.

Para medir execuções finitas depois de compilar, use:

```sh
bash tests/performance/benchmark.sh official 125 5 5
```

O script executa cinco repetições com o cliente fornecido, `N = 125` e `X = 5 ms`, valida os relatórios e escreve CSV em `stdout`. Troque `official` por `controlled` para usar a carga controlada. As duas cargas têm demandas diferentes; compare medições apenas quando cenário, versão e condições forem equivalentes. A [verificação da primeira parte](docs/verification.md#us27--verificação-da-primeira-parte) registra a matriz de requisitos e os testes executados; a [avaliação da compactação do Analista](docs/verification.md#avaliação-do-fsync-por-lote-do-analista) registra as medições posteriores dessa mudança.

## Organização do repositório

| Caminho | Responsabilidade |
|---|---|
| `src/domain/` | Registro do cliente, fila limitada, seleção EDF e métricas, sem I/O ou processos. |
| `src/runtime/` | Contextos, pilhas e escalonamento das threads de usuário. |
| `src/platform/` | Processos, sinais, semáforos, arquivos, terminal e relógio. |
| `src/app/` | Coordenação entre Recepção, Atendente, Analista e eventos. |
| `src/service_main.c` e `src/analyst_main.c` | Entrada da CLI e execução do processo Analista. |
| `provided/` | Fonte do cliente do enunciado e registro de sua proveniência. |
| [`tests/`](tests/README.md) | Testes organizados por camada, interface de comando, fixtures e medições. |
| `build/` e `run/` | Binários e dados temporários gerados; ambos ignorados pelo Git. |

Para aprofundar: [arquitetura e requisitos](docs/architecture.md), [histórias e critérios de aceitação](docs/user-stories.md), [verificações executadas](docs/verification.md), [guia de estudo em três partes](docs/team-study-guide.md) e [instruções de desenvolvimento](AGENTS.md). Mudanças prontas para revisão podem seguir o [modelo de PR](.github/pull_request_template.md).
