# yuri-os

Este repositório contém o início da implementação da primeira parte do trabalho prático: um atendimento de processos clientes, com Recepção e Atendente executados por um escalonador próprio de threads em espaço de usuário. O cliente fornecido pelo professor e o processo Analista compõem o fluxo planejado. A implementação usa C17 para Linux/glibc.

US01–US12 estão implementadas; a preparação da US13 e a Recepção da US14 também estão disponíveis: registro e transições de estado, fila limitada, métricas com tempos fornecidos pelo chamador, escalonador cooperativo, adaptador de processos, leitura serializada da demanda, IPC, arquivo de PIDs, processo Analista e coordenação da drenagem. A fila reserva uma vaga para o cliente em inicialização e escolhe o próximo pelo menor prazo absoluto. As métricas usam a resposta desde a chegada, incluindo espera e serviço. O runtime alterna contextos próprios e oferece espera por condição; os adaptadores de plataforma criam filhos reais, recolhem estados sem bloquear e consomem `demanda.txt` após a parada do PID esperado. O atendimento completo e a conferência do cliente oficial continuam pendentes; `service N X` já valida e prepara a execução.

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
