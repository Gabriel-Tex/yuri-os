# Cliente fornecido

`client.c` transcreve a listagem `cliente.c` na página 3 do [enunciado](../docs/descfinal.pdf), identificada no cabeçalho como versão `20242/trab/cliente.c`. O usuário confirmou essa página como fonte da entrega em 2026-11-09. O PDF no repositório é idêntico ao `work/descfinal.pdf` do ZIP de estudos. A listagem foi extraída com `pdftotext -layout` e conferida com a página renderizada.

- SHA-256 do PDF `62bede1dca91ce93139f8e20016e25c2b654f91c12ea72dcad05db113de914d5`
- SHA-256 da transcrição `915feefbe60d63bb32fc1d348eefefadb740dfefff53211c0d3319b641f63ced`

O cliente grava um valor decimal sem quebra de linha em `demanda.txt` antes de se parar com `SIGSTOP`. Após `SIGCONT`, usa `/sem_atend` e passa a demanda de 1, 5 ou 15 diretamente a `usleep`, em microssegundos. A inicialização de `rand` com `time(NULL)` pode produzir a mesma demanda em processos iniciados no mesmo segundo. O código e sua temporização foram preservados. `make client` compila a transcrição e `make test-client` verifica o serviço com o executável resultante.
