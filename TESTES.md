# Verificações do projeto

Para compilar e executar as verificações no Linux ou no WSL:

```bash
bash testes.sh
```

As verificações compilam os três módulos e executam os seguintes cenários:

| Módulo | Entrada / semente | Resultado verificado |
| --- | --- | --- |
| Escalonador | `entrada_alternancia.txt` | Execução concluída e relatório final |
| Escalonador | `entrada_prioridade.txt` | Execução concluída e relatório final |
| Escalonador | `entrada_loteria.txt` | Execução concluída e relatório final |
| Escalonador | `entrada_cfs.txt` | Execução concluída e relatório final |
| Memória | `entrada1.txt` | `21\|21\|23\|16\|empate` |
| Memória | `entrada2.txt` | `13\|12\|18\|9\|LRU` |
| Memória | `entrada3.txt` | `4\|4\|7\|3\|empate` |
| Memória | `entradaMemoria.txt` | `38\|40\|35\|27\|NUF` |
| E/S | `entrada_ES.txt`, seed 42 | Tempo total: 96; trocas LRU: 40 |
| E/S | `entrada_ES.txt`, seed 8 | Tempo total: 85; trocas LRU: 40 |

**Observação:** as quatro verificações de escalonamento conferem execução e relatório final, não uma comparação detalhada de cada evento do escalonador. Os demais testes conferem os resumos indicados. O programa pode imprimir logs extensos durante a simulação; o script os captura temporariamente.

As entradas fazem parte do projeto original e foram preservadas.
