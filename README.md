# yuri-os

Este repositório contém o início da implementação da primeira parte do trabalho prático: um atendimento de processos clientes, com Recepção e Atendente executados por um escalonador próprio de threads em espaço de usuário. O cliente fornecido pelo professor e o processo Analista compõem o fluxo planejado. A implementação usa C17 para Linux/glibc.

US01–US15 estão implementadas como módulos: registro e transições de estado, fila limitada, métricas, escalonador cooperativo, processos, leitura serializada da demanda, IPC, Analista, Recepção e Atendente. O Atendente confirma o término normal de cada cliente antes de contabilizá-lo e registrar seu PID. A execução finita integrada e a conferência do cliente oficial continuam pendentes; `service N X` ainda valida e prepara a execução.

Para compilar e verificar o domínio, instale um compilador C17 e GNU Make:

```sh
make          # Compila doze testes C, a fixture, `analyst` e `service` em build/.
make test     # Executa os doze testes C e o teste da CLI, inclusive filhos reais e semáforos.
make debug    # Compila e testa com -O0 -g3 em build/debug/.
make clean    # Remove somente build/; preserva fontes e run/.
```

`CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` e `LDLIBS` podem ser fornecidos ao Make; C17 e os avisos `-Wall -Wextra -Wpedantic` são acrescentados à compilação. Objetos, dependências de cabeçalhos e binários ficam em `build/`, ignorado pelo Git junto a `run/`. `build/controlled_client` é apenas uma fixture de teste, não o cliente do professor. A Recepção já gera clientes em testes isolados. O executável `service N X` valida a entrada e prepara os recursos, mas retorna código 2 com diagnóstico enquanto o atendimento não está integrado; não apresenta taxa ou conclusão fictícia. O cliente oficial será integrado na US21.

- [Arquitetura](docs/architecture.md): requisitos, decisões técnicas, módulos, fluxos e tratamento de recursos.
- [Histórias de usuário](docs/user-stories.md): ordem de desenvolvimento, critérios de aceitação e rastreabilidade.
- [Registro de verificação](docs/verification.md): comandos executados, resultados e limitações.
- [Instruções para agentes](AGENTS.md): limites do trabalho e convenções para implementação e revisão.
- [Template de PR](.github/pull_request_template.md): estrutura sugerida para descrever mudanças revisáveis.
