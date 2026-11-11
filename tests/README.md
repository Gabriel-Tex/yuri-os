# Testes

A suíte acompanha as camadas do projeto. Os arquivos ficam perto dos testes da mesma responsabilidade; os executáveis continuam em `build/` com os nomes anteriores para preservar os comandos do Makefile.

Ao acrescentar um caso, coloque o teste na pasta do módulo exercitado. Reserve `cli/` para invocações da interface pública, `fixtures/` para programas auxiliares e `performance/` para medições; mantenha o nome `*_test.c` ou `*_test.sh` para testes executáveis.

| Diretório | Conteúdo |
|---|---|
| `domain/` | Regras puras de cliente, fila e métricas, com dados e tempos determinísticos. |
| `runtime/` | Contextos, alternância, espera e despertar das threads de usuário. |
| `platform/` | Processos, demanda, semáforos, arquivo, terminal e processo Analista. |
| `app/` | Fluxos de Recepção, Atendente e serviço, incluindo parada e limpeza. |
| `cli/` | Testes de ponta a ponta pela interface de comando e validador AWK do relatório finito. |
| `fixtures/` | Cliente controlado e Analista lento usados para reproduzir estados e falhas. |
| `performance/` | Repetição de cenários finitos para medir satisfação e duração. |

## Execução

Na raiz do repositório, execute as suítes **em sequência**: os testes de integração usam os mesmos nomes de semáforo POSIX.

```sh
make
timeout -k 5s 120s make test
timeout -k 5s 120s make test-client
timeout -k 5s 120s make debug
```

`make test` usa `build/controlled_client` para testar estados e falhas reproduzíveis. `make test-client` usa o cliente fornecido em `provided/client.c` nos modos finito e infinito. `make debug` recompila em `build/debug/` com `-O0 -g3` e executa a suíte controlada. O watchdog é externo: ele não altera a paciência de um cliente no produto.

Para executar um teste C individual, compile com `make` e consulte a linha correspondente no alvo `test` do [Makefile](../Makefile). Testes que criam processos recebem caminhos absolutos dos executáveis como argumentos; outros, como `build/domain_test`, não recebem argumentos.

O benchmark fica fora do alvo `test`, pois mede desempenho e não deve tornar a suíte funcional dependente de resultados de tempo:

```sh
bash tests/performance/benchmark.sh official 125 5 5
```

O comando emite CSV em `stdout` após validar cinco execuções finitas. Use `controlled` para a fixture; compare apenas séries com condições equivalentes. Resultados e limitações da revisão verificada constam em [docs/verification.md](../docs/verification.md).
