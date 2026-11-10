# yuri-os

Este repositório implementa os modos finito e infinito da primeira parte do trabalho prático: atendimento de processos clientes, com Recepção e Atendente executados por um escalonador próprio de threads em espaço de usuário e um processo Analista. A implementação usa C17 para Linux/glibc.

A Recepção cria os dois semáforos antes do Analista e dos clientes. O Atendente confirma saída zero e disponibilidade de `/sem_atend` antes de registrar uma conclusão; seus pedidos de despertar também conduzem a drenagem do arquivo de PIDs.

A entrega ainda tem pendências de conformidade com o diagrama e de correção em casos de falha, entrada contínua e valores extremos de X. A revisão final depende dos testes de aceite do épico 8. [Pendências e critérios de fechamento](docs/user-stories.md#e08--conformidade-e-fechamento-da-primeira-parte).

No modo infinito, `build/service 0 X` gera clientes até receber `s` em TTY sem Enter ou em pipe. O atendimento ativo termina; os clientes ainda à espera são cancelados. O relatório usa apenas conclusões no denominador e informa taxa não aplicável se nenhuma ocorreu. A entrada e os recursos próprios são restaurados na saída controlada.

`SIGINT` e `SIGTERM` encerram a execução com diagnóstico e status de erro, recolhem os filhos e restauram o terminal e os sinais. Falhas do cliente, do Analista e de I/O também produzem relatório parcial apenas em `stderr`.

O Atendente confirma o término normal de cada cliente antes de contabilizá-lo e registrar seu PID. A fonte de `build/client` é a listagem do enunciado confirmada para a entrega. A suíte também usa um cliente controlado para limites e falhas. [Proveniência do cliente](provided/README.md).

Para compilar em Linux com glibc, instale um compilador C17 e GNU Make. Os testes também usam Bash e `timeout` do GNU Coreutils.

```sh
make             # Compila service, analyst, os testes e o cliente fornecido.
make client      # Compila build/client a partir de provided/client.c.
make test        # Executa os testes C e da CLI, inclusive filhos reais e semáforos.
make test-client # Verifica os dois modos usando o cliente do enunciado.
make debug       # Compila e testa com -O0 -g3 em build/debug/.
make clean       # Remove build/ e preserva fontes e run/.
```

Execute `make` e depois `build/service N X`. Use N inteiro não negativo e X inteiro positivo em milissegundos. Por exemplo, `build/service 125 5` atende 125 clientes e `build/service 0 5` aguarda `s`. Se o professor distribuir uma revisão, preserve o conteúdo original em `provided/client.c` e registre a nova origem e versão em `provided/README.md`.
`CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` e `LDLIBS` podem ser fornecidos ao Make. O código próprio compila com C17 e `-Wall -Wextra -Wpedantic`. `CLIENT_CFLAGS` configura as opções da fonte fornecida separadamente. Objetos, dependências de cabeçalhos e binários ficam em `build/`, ignorado pelo Git junto a `run/`. Sem a fonte avulsa, `make test` usa `build/controlled_client` por meio de `SERVICE_CLIENT_PATH`.

O relatório aparece depois dos PIDs e da limpeza, com criados, concluídos, satisfeitos, insatisfeitos, taxa e tempo total em milissegundos. No modo finito, a taxa é satisfeitos/N. No infinito, é satisfeitos/concluídos.

Depois de `make`, `bash tests/benchmark.sh official 125 5 5` mede cinco execuções finitas com N = 125 e X = 5 ms e escreve CSV em `stdout`. Use `controlled` para a carga da fixture. Cada linha conserva os contadores e o tempo do relatório, e uma falha interrompe a série. O registro de verificação descreve as condições e os resultados das duas cargas.

- [Arquitetura](docs/architecture.md): requisitos, decisões técnicas, módulos, fluxos e tratamento de recursos.
- [Histórias de usuário](docs/user-stories.md): ordem de desenvolvimento, critérios de aceitação e rastreabilidade.
- [Registro de verificação](docs/verification.md): comandos executados, resultados e limitações.
- [Instruções para agentes](AGENTS.md): limites do trabalho e convenções para implementação e revisão.
- [Template de PR](.github/pull_request_template.md): estrutura sugerida para descrever mudanças revisáveis.
