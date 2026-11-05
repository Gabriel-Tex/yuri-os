# yuri-os

Este repositório contém o início da implementação da primeira parte do trabalho prático: um atendimento de processos clientes, com Recepção e Atendente executados por um escalonador próprio de threads em espaço de usuário. O cliente fornecido pelo professor e o processo Analista compõem o fluxo planejado. A implementação usa C17 para Linux/glibc.

US01–US04 estão implementadas: registro e transições de estado, fila limitada, métricas com tempos fornecidos pelo chamador e contextos independentes de threads de usuário. A fila reserva uma vaga para o cliente em inicialização e escolhe o próximo pelo menor prazo absoluto. As métricas usam a resposta desde a chegada, incluindo espera e serviço. O runtime cria duas pilhas e permite despacho manual e retorno ao escalonador; a política automática e a espera por eventos vêm nas US05–US06. As decisões de implementação devem buscar maximizar satisfação e minimizar o tempo total, respeitando os requisitos e a carga oficial. Ainda não há serviço executável nem integração com processos; `service N X` permanece a interface planejada.

Para compilar e verificar o domínio, instale um compilador C17 e GNU Make:

```sh
make          # Compila os testes de domínio, conversão e runtime em build/.
make test     # Executa os quatro testes; o runtime usa ucontext.
make debug    # Compila e testa com -O0 -g3 em build/debug/.
make clean    # Remove somente build/; preserva fontes e run/.
```

`CC`, `CPPFLAGS`, `CFLAGS`, `LDFLAGS` e `LDLIBS` podem ser fornecidos ao Make; C17 e os avisos `-Wall -Wextra -Wpedantic` são acrescentados à compilação. Objetos, dependências de cabeçalhos e binários ficam em `build/`, ignorado pelo Git junto a `run/`. Os alvos do serviço, Analista e cliente serão adicionados conforme suas fontes existirem (US20).

- [Arquitetura](docs/architecture.md): requisitos, decisões técnicas, módulos, fluxos e tratamento de recursos.
- [Histórias de usuário](docs/user-stories.md): ordem de desenvolvimento, critérios de aceitação e rastreabilidade.
- [Registro de verificação](docs/verification.md): comandos executados, resultados e limitações.
- [Instruções para agentes](AGENTS.md): limites do trabalho e convenções para implementação e revisão.
- [Template de PR](.github/pull_request_template.md): estrutura sugerida para descrever mudanças revisáveis.
