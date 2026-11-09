# yuri-os

Este repositório implementa os modos finito e infinito da primeira parte do trabalho prático: atendimento de processos clientes, com Recepção e Atendente executados por um escalonador próprio de threads em espaço de usuário e um processo Analista. A implementação usa C17 para Linux/glibc.

No modo infinito, `build/service 0 X` gera clientes até receber `s` em TTY sem Enter ou em pipe. O atendimento ativo termina; os clientes ainda à espera são cancelados. O relatório usa apenas conclusões no denominador e informa taxa não aplicável se nenhuma ocorreu. A entrada e os recursos próprios são restaurados na saída controlada.

`SIGINT` e `SIGTERM` encerram a execução com diagnóstico e status de erro, recolhem os filhos e restauram o terminal e os sinais. Falhas do cliente, do Analista e de I/O também produzem relatório parcial apenas em `stderr`.

US01–US19 estão implementadas com cliente controlado: registro e transições de estado, fila limitada, métricas, escalonador cooperativo, processos, leitura serializada da demanda, IPC, Analista, Recepção, Atendente, relatórios e limpeza controlada. O Atendente confirma o término normal de cada cliente antes de contabilizá-lo e registrar seu PID. A fonte oficial do cliente ainda não está disponível em `provided/client.c`; sua conferência e integração pertencem à US21.

Para compilar e verificar o domínio, instale um compilador C17 e GNU Make:

```sh
make          # Compila os testes, a fixture, `analyst` e `service` em build/.
make test     # Executa os testes C e da CLI, inclusive filhos reais e semáforos.
make debug    # Compila e testa com -O0 -g3 em build/debug/.
make clean    # Remove somente build/; preserva fontes e run/.
```

`CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` e `LDLIBS` podem ser fornecidos ao Make; C17 e os avisos `-Wall -Wextra -Wpedantic` são acrescentados à compilação. Objetos, dependências de cabeçalhos e binários ficam em `build/`, ignorado pelo Git junto a `run/`. `build/controlled_client` é apenas uma fixture de teste, não o cliente do professor. `build/service N X` procura `build/client`; enquanto ele não estiver presente, a execução informa erro. Os testes indicam explicitamente `SERVICE_CLIENT_PATH=/caminho/absoluto/build/controlled_client`, sem alterar o contrato de argumentos do serviço. O relatório aparece depois dos PIDs e da limpeza, com criados, concluídos, satisfeitos, insatisfeitos, taxa e tempo total em milissegundos. No modo finito, a taxa é satisfeitos/N; no infinito, é satisfeitos/concluídos.

- [Arquitetura](docs/architecture.md): requisitos, decisões técnicas, módulos, fluxos e tratamento de recursos.
- [Histórias de usuário](docs/user-stories.md): ordem de desenvolvimento, critérios de aceitação e rastreabilidade.
- [Registro de verificação](docs/verification.md): comandos executados, resultados e limitações.
- [Instruções para agentes](AGENTS.md): limites do trabalho e convenções para implementação e revisão.
- [Template de PR](.github/pull_request_template.md): estrutura sugerida para descrever mudanças revisáveis.
