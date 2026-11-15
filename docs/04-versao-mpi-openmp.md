# 4. Versão MPI + OpenMP (guia de desenvolvimento)

> **Situação: não implementada neste repositório.** Este guia mostra como desenvolvê-la a partir de `sequencial/sir_seq.c`. Os trechos de código são esboços (não foram compilados nem testados) e os comandos de compilação e execução são os recomendados, a confirmar no NPAD.

## 4.1 Ideia

- **MPI (memória distribuída):** decomposição 1D por linhas. Cada um dos P processos fica com um bloco de linhas consecutivas da grade.
- **Halo:** cada processo guarda uma linha extra em cima e outra embaixo do seu bloco (são as linhas fantasmas da versão sequencial). A cada iteração, ele envia sua primeira e sua última linha reais aos vizinhos e recebe as deles nessas linhas extras.
- **OpenMP (memória compartilhada):** dentro de cada processo, as linhas do bloco são divididas entre T threads.
- **Agregação global:** as contagens S, I e R de cada processo são somadas com `MPI_Reduce`.

```
processo 0   [ halo de cima: borda fixa (sempre S)     ]
             [ linhas globais 0 .. a-1                 ]
             [ halo de baixo <- 1ª linha do processo 1 ]
processo 1   [ halo de cima  <- última linha do proc. 0 ]
             [ linhas globais a .. b-1                 ]
             [ halo de baixo <- 1ª linha do processo 2 ]
...
```

## 4.2 Passo a passo do desenvolvimento

### Passo 1: criar o arquivo a partir da versão sequencial

```bash
mkdir -p mpi_openmp
cp sequencial/sir_seq.c mpi_openmp/sir_mpi_omp.c
```

Mantenha `misturar`, `aleatorio`, `vizinhas_infectadas`, `salvar_csv` e `salvar_ppm` sem alterações.

### Passo 2: inicializar e finalizar o MPI

```c
#include <mpi.h>
#include <omp.h>

int nivel, rank, nprocs;
MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &nivel);  /* só a thread mestre chama MPI */
MPI_Comm_rank(MPI_COMM_WORLD, &rank);
MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
/* ... */
MPI_Finalize();
```

Todos os processos leem os mesmos argumentos. Só o processo 0 imprime e grava arquivos. Exija `N >= nprocs` (cada processo precisa de pelo menos uma linha).

### Passo 3: dividir as linhas entre os processos

```c
int base = n / nprocs, resto = n % nprocs;
int linhas = base + (rank < resto ? 1 : 0);                /* linhas deste processo */
int inicio = rank * base + (rank < resto ? rank : resto);   /* 1ª linha global (base 0) */
```

Exemplo: N = 10 e P = 4 dão blocos de 3, 3, 2 e 2 linhas, começando nas linhas 0, 3, 6 e 8.

### Passo 4: alocar o bloco local com halo

```c
const int largura = n + 2;
const size_t total_local = (size_t)(linhas + 2) * largura;
unsigned char *atual = calloc(total_local, 1);     /* tudo S, inclusive halos e bordas */
unsigned char *proxima = calloc(total_local, 1);
```

A linha local `i` (1 ≤ i ≤ `linhas`) corresponde à linha global `inicio + i - 1`. As colunas fantasmas continuam sempre S, como na versão sequencial.

### Passo 5: condição inicial em coordenadas globais

```c
const int centro = n / 2;
if (centro >= inicio && centro < inicio + linhas)            /* só o dono da linha */
    atual[(size_t)(centro - inicio + 1) * largura + (centro + 1)] = INFECTADO;
```

### Passo 6: trocar os halos no início de cada iteração

No `main`, antes do laço de iterações, defina os vizinhos:

```c
const int cima  = (rank > 0) ? rank - 1 : MPI_PROC_NULL;
const int baixo = (rank < nprocs - 1) ? rank + 1 : MPI_PROC_NULL;
```

E crie a função de troca (fora do `main`):

```c
static void trocar_halos(unsigned char *g, int linhas, int largura, int cima, int baixo)
{
    /* envia a 1ª linha real para cima e recebe o halo de baixo */
    MPI_Sendrecv(&g[(size_t)1 * largura], largura, MPI_UNSIGNED_CHAR, cima, 0,
                 &g[(size_t)(linhas + 1) * largura], largura, MPI_UNSIGNED_CHAR, baixo, 0,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    /* envia a última linha real para baixo e recebe o halo de cima */
    MPI_Sendrecv(&g[(size_t)linhas * largura], largura, MPI_UNSIGNED_CHAR, baixo, 1,
                 &g[0], largura, MPI_UNSIGNED_CHAR, cima, 1,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}
```

Com `MPI_PROC_NULL`, a comunicação com um vizinho inexistente não faz nada. Assim, o halo de cima do processo 0 e o de baixo do último processo continuam S (borda fixa do domínio). A troca deve ser feita na grade `atual`, antes de calcular a iteração.

### Passo 7: atualização local com OpenMP e linha global no sorteio

A função `passo` passa a receber `linhas` e `inicio`. O corpo do laço é o mesmo da versão sequencial, mudando apenas a linha usada no sorteio:

```c
static void passo(const unsigned char *atual, unsigned char *proxima, const Parametros *p,
                  int linhas, int inicio, int iteracao, long long contagem[3])
{
    const int n = p->n, largura = n + 2;
    long long s = 0, inf = 0, r = 0;

    #pragma omp parallel for reduction(+ : s, inf, r) schedule(static)
    for (int i = 1; i <= linhas; i++) {
        const int linha_global = inicio + i - 1;
        for (int j = 1; j <= n; j++) {
            /* ... igual à versão sequencial, mas com
               aleatorio(p->semente, iteracao, linha_global, j - 1) ... */
        }
    }
    contagem[SUSCETIVEL] = s;
    contagem[INFECTADO] = inf;
    contagem[RECUPERADO] = r;
}
```

### Passo 8: agregação global

```c
long long local[3], global[3];
passo(atual, proxima, &p, linhas, inicio, t, local);
MPI_Reduce(local, global, 3, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
if (rank == 0)
    memcpy(&serie[3 * ((size_t)t + 1)], global, sizeof global);
```

### Passo 9: medir o tempo

```c
MPI_Barrier(MPI_COMM_WORLD);
const double inicio_tempo = MPI_Wtime();
for (int t = 0; t < p.iteracoes; t++) {
    trocar_halos(atual, linhas, largura, cima, baixo);
    /* passo + MPI_Reduce (passo 8) */
    unsigned char *troca = atual; atual = proxima; proxima = troca;
}
MPI_Barrier(MPI_COMM_WORLD);
const double tempo = MPI_Wtime() - inicio_tempo;   /* o processo 0 imprime */
```

Para a análise de overhead e de balanceamento (docs/07), acumule também, em cada processo, o tempo gasto em comunicação (`trocar_halos` + `MPI_Reduce`) e o tempo de cálculo (`passo`). No fim, reduza esses valores com `MPI_MAX` e `MPI_MIN` e imprima no processo 0.

### Passo 10: saídas

- Imprima as mesmas linhas da versão sequencial ("Estado final", "Tempo da simulação"). Pode acrescentar uma linha, como `Processos MPI: P | Threads por processo: T` (use `omp_get_max_threads()`).
- O processo 0 grava o CSV (ele tem a série completa).
- A imagem exige a grade inteira no processo 0. Junte as linhas com `MPI_Gatherv` só quando `PREFIXO_SAIDA` for informado, fora da região cronometrada:

```c
int *qtd = NULL, *desl = NULL;
unsigned char *grade_total = NULL;
if (rank == 0) {
    qtd = malloc(nprocs * sizeof(int));
    desl = malloc(nprocs * sizeof(int));
    for (int q = 0; q < nprocs; q++) {
        int linhas_q = base + (q < resto ? 1 : 0);
        int inicio_q = q * base + (q < resto ? q : resto);
        qtd[q] = linhas_q * largura;
        desl[q] = (inicio_q + 1) * largura;          /* +1: pula a linha fantasma de cima */
    }
    grade_total = calloc((size_t)largura * largura, 1);
}
MPI_Gatherv(&atual[largura], linhas * largura, MPI_UNSIGNED_CHAR,
            grade_total, qtd, desl, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
if (rank == 0)
    salvar_ppm(arquivo, grade_total, n);             /* mesma função da versão sequencial */
```

### Passo 11: validar

A saída tem de ser idêntica à da versão sequencial, para qualquer número de processos e threads (inclusive quando P não divide N):

```bash
bash ../sequencial/validar.sh "mpirun ./sir_mpi_omp" ../sequencial/sir_seq
```

Rode dentro de um job (seção 4.6), com P ≤ 101 processos (as grades de teste têm 101 linhas). Os testes 3 e 4 (frente e anel determinísticos) atravessam as fronteiras entre processos e detectam erros na troca de halos. O teste 6 compara CSV e imagem com a versão sequencial.

## 4.3 Pré-requisitos

- GCC com OpenMP e Open MPI (módulos da seção 4.4).
- A versão sequencial compilada em `sequencial/sir_seq` (referência para a validação).

## 4.4 Preparação do ambiente

```bash
module load compilers/gnu/14.2.0
module load libraries/openmpi/5.0.10-gnu14-ucxmt
which mpicc && mpicc --version      # confirme o compilador usado pelo MPI
```

## 4.5 Compilação

```bash
cd mpi_openmp
mpicc -O3 -std=c11 -Wall -fopenmp -o sir_mpi_omp sir_mpi_omp.c
```

Use o mesmo `-O3` da versão sequencial.

## 4.6 Execução

Teste rápido no nó de login (só grades pequenas e poucos processos, se o `mpirun` funcionar fora do Slurm):

```bash
OMP_NUM_THREADS=2 mpirun -np 2 ./sir_mpi_omp 512 300 0.3 0.1 42 teste
```

Job Slurm (exemplo `job_mpi_omp.sh`, com P = 4 processos × T = 8 threads em um nó):

```bash
#!/bin/bash
#SBATCH --job-name=sir-mpi-omp
#SBATCH --partition=amd-512
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=4          # P: processos MPI por nó
#SBATCH --cpus-per-task=8            # T: threads OpenMP por processo
#SBATCH --hint=compute_bound         # 1 thread por núcleo físico
#SBATCH --exclusive
#SBATCH --time=0-0:30
#SBATCH --output=sir-mpi-omp-%j.out
#SBATCH --error=sir-mpi-omp-%j.err

module load compilers/gnu/14.2.0
module load libraries/openmpi/5.0.10-gnu14-ucxmt

export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_PLACES=cores
export OMP_PROC_BIND=close

# 1) validação antes de medir
bash ../sequencial/validar.sh "mpirun ./sir_mpi_omp" ../sequencial/sir_seq || exit 1

# 2) medição: o Slurm informa ao mpirun quantos processos usar (não precisa de -np);
#    cada processo recebe T núcleos
mpirun --map-by slot:PE=$SLURM_CPUS_PER_TASK --bind-to core --report-bindings ./sir_mpi_omp 8192 500
```

- Confira no `.err` as linhas do `--report-bindings`: cada processo deve estar associado a T núcleos diferentes. Se a opção `--map-by slot:PE=...` não for aceita pela versão do Open MPI carregada, teste `srun --cpus-per-task=$SLURM_CPUS_PER_TASK ./sir_mpi_omp 8192 500`. O tutorial de MPI do NPAD recomenda testar `mpirun` e `srun` em caso de dúvida.
- Vários nós: aumente `--nodes` (ex.: `--nodes=2 --ntasks-per-node=4 --cpus-per-task=32`).
- Para varrer configurações, submeta um job por combinação; as opções na linha de comando do `sbatch` substituem as do script:

```bash
for P in 1 2 4 8; do
    for T in 1 2 4 8 16; do
        sbatch --ntasks-per-node=$P --cpus-per-task=$T job_mpi_omp.sh
    done
done
```

## 4.7 Parâmetros

Os mesmos da versão sequencial (`N ITERACOES [BETA] [GAMA] [SEMENTE] [PREFIXO_SAIDA]`). O número de processos (P) vem de `--ntasks`/`--ntasks-per-node` e o de threads (T) de `--cpus-per-task` (via `OMP_NUM_THREADS`). É preciso N ≥ P.

## 4.8 Verificação e obtenção dos resultados

- No `.out`: os testes de `validar.sh` com `[PASSOU]` (inclusive o teste 6) e as linhas "Estado final" e "Tempo da simulação".
- Para a mesma entrada, a linha "Estado final" deve ser idêntica à da versão sequencial (ex.: `./sir_mpi_omp 512 300 0.3 0.1 42` deve dar `S = 33136 | I = 11411 | R = 217597`).
- Registre os tempos no formato comum (docs/07): `versao,n,iteracoes,processos,threads,repeticao,tempo_s`.
- Copie os resultados para o seu computador com `scp` (docs/01, seção 1.9).

## 4.9 Experimentos específicos desta versão

Exigidos pela especificação do projeto (detalhes em [docs/07](07-experimentos-e-metricas.md)):

- diferentes tamanhos de entrada (N);
- variação do número de processos MPI (P) e de threads OpenMP (T), inclusive combinações com o mesmo total P × T (ex.: 1×32, 2×16, 4×8, 8×4, 32×1);
- escalabilidade forte e fraca;
- overhead de comunicação (tempo de `trocar_halos` + `MPI_Reduce` em relação ao total);
- balanceamento de carga (tempo de cálculo máximo × mínimo entre processos).
