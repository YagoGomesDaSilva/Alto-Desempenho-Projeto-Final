# Simulador de propagação SIR em grade

Projeto final da disciplina **DIM1116 – Computação de Alto Desempenho** (IMD/UFRN, 2026.2).

Inspirado no jogo *Plague Inc.*, o projeto simula a propagação de uma "infecção" por uma grade de regiões, combinando características de modelos de propagação de incêndio e de difusão de temperatura. Cada célula da grade está **suscetível (S)**, **infectada (I)** ou **recuperada (R)**, e a cada iteração seu estado é atualizado a partir das células vizinhas e de parâmetros de transmissão. É um problema **BSP** (*Bulk Synchronous Parallel*) do tipo *stencil*/autômato celular: cada iteração tem computação local, troca de bordas entre vizinhos e uma agregação global (contagem de S, I e R).

## Versões

| Versão | Pasta | Situação |
|---|---|---|
| Sequencial (baseline) | [`sequencial/`](sequencial/) | Implementada e testada localmente (Ubuntu 24.04 no WSL, GCC 13.3). **Ainda não executada no NPAD.** |
| MPI + OpenMP (CPU) | `mpi_openmp/` (a criar) | A desenvolver: guia em [docs/04](docs/04-versao-mpi-openmp.md) |
| OpenMP em GPU | `openmp_gpu/` (a criar) | A desenvolver: guia em [docs/05](docs/05-versao-openmp-gpu.md) |
| CUDA | `cuda/` (a criar) | A desenvolver: guia em [docs/06](docs/06-versao-cuda.md) |

## Equipe

| Integrante | Responsabilidade |
|---|---|
| Yago Gomes da Silva | Versão sequencial (baseline) e documentação |
| Davi Alves do Nascimento | *(a definir)* |
| Eduarda Rodrigues Chiesa | *(a definir)* |
| Tobias dos Santos Neto | *(a definir)* |

## Estrutura do repositório

```
.
├── README.md
├── docs/                          passo a passo (ler na ordem)
│   ├── 01-ambiente-npad.md
│   ├── 02-modelo-e-algoritmo.md
│   ├── 03-versao-sequencial.md
│   ├── 04-versao-mpi-openmp.md
│   ├── 05-versao-openmp-gpu.md
│   ├── 06-versao-cuda.md
│   └── 07-experimentos-e-metricas.md
└── sequencial/
    ├── sir_seq.c                  simulador (versão sequencial)
    ├── validar.sh                 testes de corretude (servem para todas as versões)
    └── job_sequencial.sh          job Slurm para o NPAD
```

## Interface comum a todas as versões

Todas as versões devem aceitar os mesmos argumentos e imprimir as mesmas linhas. Assim, os scripts de execução e de validação funcionam para qualquer versão:

```
./<programa> N ITERACOES [BETA] [GAMA] [SEMENTE] [PREFIXO_SAIDA]
```

| Argumento | Significado | Padrão |
|---|---|---|
| `N` | lado da grade (N × N células) | obrigatório |
| `ITERACOES` | número de iterações | obrigatório |
| `BETA` | probabilidade de transmissão por vizinha infectada, por iteração | 0.3 |
| `GAMA` | probabilidade de recuperação por iteração | 0.1 |
| `SEMENTE` | semente do gerador pseudoaleatório | 42 |
| `PREFIXO_SAIDA` | se informado, grava `PREFIXO_serie.csv` e `PREFIXO_final.ppm` | (não grava) |

Linhas que toda versão deve imprimir (os scripts dependem delas):

```
Estado final: S = <n> | I = <n> | R = <n>
Tempo da simulação (s): <segundos>
```

Como o gerador aleatório não tem estado (ver [docs/02](docs/02-modelo-e-algoritmo.md)), **todas as versões devem produzir exatamente a mesma saída** (mesmo CSV e mesma imagem) para a mesma entrada. Essa é a forma de validar as versões paralelas.

## Início rápido (versão sequencial)

```bash
cd sequencial
gcc -O3 -std=c11 -Wall -o sir_seq sir_seq.c
bash validar.sh
./sir_seq 512 300 0.3 0.1 42 exemplo
```

No NPAD, as medições são feitas com `sbatch job_sequencial.sh` (detalhes em [docs/03](docs/03-versao-sequencial.md)).

## Passo a passo

1. [Preparação do ambiente no NPAD](docs/01-ambiente-npad.md): acesso, cópia de arquivos, módulos e Slurm.
2. [Modelo e algoritmo](docs/02-modelo-e-algoritmo.md): regras do modelo SIR e o que toda versão precisa respeitar.
3. [Versão sequencial](docs/03-versao-sequencial.md): compilação, execução, validação e execução no NPAD.
4. [Versão MPI + OpenMP](docs/04-versao-mpi-openmp.md): guia de desenvolvimento, compilação e execução.
5. [Versão OpenMP em GPU](docs/05-versao-openmp-gpu.md): guia de desenvolvimento, compilação e execução.
6. [Versão CUDA](docs/06-versao-cuda.md): guia de desenvolvimento, compilação e execução.
7. [Experimentos e métricas](docs/07-experimentos-e-metricas.md): tempo, speedup, eficiência e escalabilidade forte e fraca.
