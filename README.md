# Simulador de Sistemas Operacionais em C

Projeto acadêmico desenvolvido em equipe para a disciplina de **Sistemas Operacionais**, com simulações de **escalonamento de processos, gerenciamento de memória e operações de entrada/saída (E/S)**.

**Linguagem:** C (C11) · **Ferramentas:** GCC e Make · **Ambiente:** Linux (também pode ser utilizado no Windows com WSL)

## Funcionalidades

| Módulo | O que foi implementado |
| --- | --- |
| **Escalonamento** | Round Robin (alternância circular), Prioridade, Loteria e CFS; cálculo de turnaround e tempo em estado pronto. |
| **Memória** | Simulação e comparação de FIFO, LRU, NUF e Ótimo, com políticas de substituição local e global. |
| **Entrada/saída** | Round Robin + LRU + dispositivos de E/S com capacidade simultânea, duração, fila FIFO e bloqueio de processos. |

No módulo de E/S, um processo que solicita um dispositivo passa a **bloqueado**. Se o dispositivo não tiver vaga, aguarda na fila; quando a operação termina, volta ao estado **pronto**. CPU e dispositivos avançam ao longo do tempo simulado.

## Organização

```text
.
├── escalonador/
│   ├── entradas/          # Entradas de teste da etapa 1
│   ├── include/           # Arquivos .h
│   ├── src/               # Arquivos .c
│   └── Makefile
├── memoria/
│   ├── entradas/          # Entradas de teste da etapa 2
│   ├── include/
│   ├── src/
│   └── Makefile
├── entrada_saida/
│   ├── entradas/          # Entrada de teste da etapa 3
│   ├── include/
│   ├── src/
│   └── Makefile
├── Makefile
├── testes.sh              # Verificações reproduzíveis
├── TESTES.md              # Resultados esperados e orientações
└── README.md
```

Cada módulo tem seus próprios códigos e Makefile para preservar as três etapas do projeto.

## Como executar

**Requisitos:** Linux com GCC e Make. No Ubuntu/WSL Ubuntu, instale com:

```bash
sudo apt update
sudo apt install build-essential
```

No **Windows 11**, utilize o terminal do Ubuntu pelo **WSL**. Execute os comandos abaixo a partir da pasta deste projeto, dentro do Linux/WSL.

### Compilar tudo

```bash
make
```

### Executar os três módulos

```bash
# Escalonamento de processos
make executar-escalonador

# Gerenciamento de memória
make executar-memoria

# Escalonamento + memória + entrada/saída
make executar-es
```

Para testar outros algoritmos ou entradas:

```bash
./escalonador/escalonador escalonador/entradas/entrada_cfs.txt
./escalonador/escalonador escalonador/entradas/entrada_prioridade.txt
./memoria/escalonador memoria/entradas/entrada1.txt
```

É possível escolher a semente do gerador pseudoaleatório para reproduzir eventos de E/S:

```bash
# Semente padrão 42
./entrada_saida/simulador entrada_saida/entradas/entrada_ES.txt

# Semente 8: exemplo com espera na fila de um dispositivo
./entrada_saida/simulador --seed 8 entrada_saida/entradas/entrada_ES.txt
```

### Executar verificações

```bash
bash testes.sh
```

O script compila o projeto e verifica **quatro entradas de escalonamento, quatro de memória e duas execuções de E/S**, incluindo resultados numéricos esperados nos módulos de memória e E/S. Consulte [TESTES.md](TESTES.md) para mais informações.

Para remover os arquivos de compilação:

```bash
make clean
```

## Entradas e resultados

Os arquivos de exemplo ficam nas pastas `entradas/` de cada módulo. Cada linha descreve configurações da simulação e processos; na terceira etapa, também são informados os dispositivos e as probabilidades de requisição de E/S.

Exemplo de saída final para `entrada_saida/entradas/entrada_ES.txt`, com semente **42**:

```text
PID 1: turnaround 47 | pronto 22 | bloqueado 5
PID 2: turnaround 51 | pronto 27 | bloqueado 0
PID 3: turnaround 96 | pronto 36 | bloqueado 28
Tempo total: 96
Trocas de pagina (LRU): 40
```

*Os valores acima são resultados de uma simulação educativa, não medições de um sistema operacional real.*

## Conceitos aplicados

- Filas de processos, estados **pronto**, **executando** e **bloqueado**;
- Estruturas de dados como **árvore de Fenwick** e **árvore rubro-negra**;
- Escalonamento de CPU, gerenciamento de páginas e políticas de substituição;
- Compartilhamento de dispositivos e filas de espera de E/S;
- Leitura de entradas, controle do tempo simulado e métricas por processo.

## Autoria

Projeto realizado em grupo por:

- **Antonio Ollé Ramos**
- **Yuri**
- **Guilherme Amarilho**
- **Fernando Melo**

Minha participação incluiu o trabalho com o módulo de **entrada/saída**, especialmente os arquivos responsáveis pelos dispositivos e filas. A implementação completa é resultado do trabalho coletivo.

## Nota

Este repositório apresenta um **simulador educacional**, não um kernel ou gerenciador de processos do sistema operacional hospedeiro. Os executáveis são gerados localmente e não estão incluídos no repositório.
