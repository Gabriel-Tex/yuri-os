# yuri-os

Este repositório contém o início da implementação da primeira parte do trabalho prático: um atendimento de processos clientes, com Recepção e Atendente executados por um escalonador próprio de threads em espaço de usuário. O cliente fornecido pelo professor e o processo Analista compõem o fluxo planejado. A implementação usa C17 para Linux/glibc.

US01–US09 estão implementadas: registro e transições de estado, fila limitada, métricas com tempos fornecidos pelo chamador, escalonador cooperativo, adaptador de processos, leitura serializada da demanda e IPC. A fila reserva uma vaga para o cliente em inicialização e escolhe o próximo pelo menor prazo absoluto. As métricas usam a resposta desde a chegada, incluindo espera e serviço. O runtime alterna contextos próprios e oferece espera por condição; os adaptadores de plataforma criam filhos reais, recolhem estados sem bloquear e consomem `demanda.txt` após a parada do PID esperado. A integração do serviço e a conferência do cliente oficial continuam pendentes; `service N X` permanece a interface planejada.

Para compilar e verificar o domínio, instale um compilador C17 e GNU Make:

```sh
make          # Compila sete testes e a fixture de processos em build/.
make test     # Executa os sete testes, inclusive filhos reais e semáforos POSIX.
make debug    # Compila e testa com -O0 -g3 em build/debug/.
make clean    # Remove somente build/; preserva fontes e run/.
```

`CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` e `LDLIBS` podem ser fornecidos ao Make; C17 e os avisos `-Wall -Wextra -Wpedantic` são acrescentados à compilação. Objetos, dependências de cabeçalhos e binários ficam em `build/`, ignorado pelo Git junto a `run/`. `build/controlled_client` é apenas uma fixture de teste, não o cliente do professor. Os alvos do serviço, Analista e cliente oficial serão adicionados conforme suas fontes existirem (US20–US21).

- [Arquitetura](docs/architecture.md): requisitos, decisões técnicas, módulos, fluxos e tratamento de recursos.
- [Histórias de usuário](docs/user-stories.md): ordem de desenvolvimento, critérios de aceitação e rastreabilidade.
- [Registro de verificação](docs/verification.md): comandos executados, resultados e limitações.
- [Instruções para agentes](AGENTS.md): limites do trabalho e convenções para implementação e revisão.
- [Template de PR](.github/pull_request_template.md): estrutura sugerida para descrever mudanças revisáveis.
