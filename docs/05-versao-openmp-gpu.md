# 5. Versão OpenMP em GPU (guia de desenvolvimento)

> **Situação: não implementada neste repositório.** Este guia mostra como desenvolvê-la a partir de `sequencial/sir_seq.c`. Os trechos de código são esboços (não foram compilados nem testados) e os comandos são os recomendados, a confirmar no NPAD.

## 5.1 Ideia

- O laço da função `passo` é enviado para a GPU com `#pragma omp target teams distribute parallel for`: cada célula vira uma iteração independente executada por threads da GPU.
- As duas grades ficam **na memória da GPU durante toda a simulação**: são copiadas da CPU para a GPU uma vez no início e voltam uma vez no fim. A transferência CPU↔GPU é o principal custo a medir e discutir.
- A cada iteração, só as três contagens (S, I, R) voltam para a CPU (redução).

## 5.2 Passo a passo do desenvolvimento

### Passo 1: criar o arquivo a partir da versão sequencial

```bash
mkdir -p openmp_gpu
cp sequencial/sir_seq.c openmp_gpu/sir_omp_gpu.c
```

### Passo 2: marcar as funções chamadas na GPU

```c
#include <omp.h>

#pragma omp declare target
static uint64_t misturar(uint64_t x) { /* sem alterações */ }
static double aleatorio(uint64_t semente, int iteracao, int linha, int coluna) { /* idem */ }
static int vizinhas_infectadas(const unsigned char *grade, int largura, int i, int j) { /* idem */ }
#pragma omp end declare target
```

### Passo 3: levar o laço de `passo` para a GPU

```c
static void passo(const unsigned char *atual, unsigned char *proxima, const Parametros *p,
                  int iteracao, long long contagem[3])
{
    const int n = p->n, largura = n + 2;
    const size_t total = (size_t)largura * largura;
    const uint64_t semente = p->semente;  /* cópias locais: não use o ponteiro p dentro da GPU */
    const double gama = p->gama;
    double prob[9];
    for (int k = 0; k <= 8; k++)
        prob[k] = p->prob_infeccao[k];
    long long s = 0, inf = 0, r = 0;

    #pragma omp target teams distribute parallel for collapse(2) reduction(+ : s, inf, r) \
            map(to: atual[0:total], prob[0:9]) map(from: proxima[0:total])
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= n; j++) {
            /* corpo igual ao da versão sequencial, usando semente, gama e prob[k] */
        }
    }
    contagem[SUSCETIVEL] = s;
    contagem[INFECTADO] = inf;
    contagem[RECUPERADO] = r;
}
```

- `p` é um ponteiro para a memória da CPU; por isso os parâmetros são copiados para variáveis locais, que o OpenMP leva automaticamente para a GPU.
- Pelo OpenMP 5.0, as variáveis da `reduction` em um construto combinado com `target` voltam automaticamente para a CPU. Se o compilador seguir só o OpenMP 4.5 e as contagens voltarem zeradas, acrescente `map(tofrom: s, inf, r)`. O teste de conservação do `validar.sh` detecta esse problema.

### Passo 4: manter os dados na GPU durante todas as iterações

No `main`:

```c
unsigned char *a = atual, *b = proxima;            /* endereços originais das duas grades */

#pragma omp target                                   /* "aquecimento": inicializa a GPU */
{ }

double t0 = agora();
#pragma omp target enter data map(to: a[0:total], b[0:total])     /* CPU -> GPU, uma vez */
double t1 = agora();
for (int t = 0; t < p.iteracoes; t++) {
    passo(atual, proxima, &p, t, &serie[3 * ((size_t)t + 1)]);
    unsigned char *troca = atual; atual = proxima; proxima = troca;
}
double t2 = agora();
#pragma omp target exit data map(from: a[0:total], b[0:total])    /* GPU -> CPU, uma vez */
double t3 = agora();
/* "Tempo da simulação (s)" = t2 - t1 (comparável às outras versões)
   imprima também "Tempo de transferência CPU<->GPU (s)" = (t1 - t0) + (t3 - t2) */
```

Como as grades já estão na GPU, os `map` dentro de `passo` não copiam nada: o runtime só encontra as cópias que já estão lá. Ao final, `atual` aponta para a grade com o estado final, já copiada de volta para a CPU (as duas foram mapeadas com `from`).

### Passo 5: conferir que o código roda mesmo na GPU

- Compile com `-Minfo=mp`: o `nvc` informa quais laços foram gerados para a GPU.
- Execute com `export OMP_TARGET_OFFLOAD=MANDATORY`: se não for possível usar a GPU, o programa aborta em vez de rodar silenciosamente na CPU.
- Imprima `omp_get_num_devices()` no início (deve ser ≥ 1).

### Passo 6: validar

```bash
bash ../sequencial/validar.sh ./sir_omp_gpu ../sequencial/sir_seq
```

Rode dentro de um job na partição de GPU (seção 5.6). Todos os testes, inclusive o 6 (CSV e imagem idênticos aos da versão sequencial), devem passar.

## 5.3 Pré-requisitos

- Compilador com suporte a *offload* OpenMP para GPUs NVIDIA: o `nvc` do NVIDIA HPC SDK (módulo `compilers/nvidia/nvhpc/24.11`). O GCC só faz *offload* se tiver sido compilado com esse suporte, o que não é garantido no NPAD.
- Partição com GPU (sugestão: `gpu-4-a100`). O nó de login não tem GPU, mas pode ser usado para compilar.
- A versão sequencial compilada em `sequencial/sir_seq` (referência para a validação).

## 5.4 Preparação do ambiente

```bash
module load compilers/nvidia/nvhpc/24.11
nvc --version
```

## 5.5 Compilação

```bash
cd openmp_gpu
nvc -O3 -mp=gpu -gpu=cc80 -Minfo=mp -o sir_omp_gpu sir_omp_gpu.c
```

- `-mp=gpu`: ativa o OpenMP com *offload* para GPU.
- `-gpu=cc80`: arquitetura da A100. Use `cc70` para a V100 e `cc90` para a H100/H200.

## 5.6 Execução

Job Slurm (exemplo `job_omp_gpu.sh`):

```bash
#!/bin/bash
#SBATCH --job-name=sir-omp-gpu
#SBATCH --partition=gpu-4-a100
#SBATCH --gpus-per-node=1
#SBATCH --cpus-per-task=1
#SBATCH --time=0-0:30
#SBATCH --output=sir-omp-gpu-%j.out
#SBATCH --error=sir-omp-gpu-%j.err

module load compilers/nvidia/nvhpc/24.11
export OMP_TARGET_OFFLOAD=MANDATORY

nvidia-smi                                   # registra qual GPU foi usada
bash ../sequencial/validar.sh ./sir_omp_gpu ../sequencial/sir_seq || exit 1
for N in 1024 2048 4096 8192 16384; do
    ./sir_omp_gpu $N 500
done
```

- Não use `--exclusive` nas partições de GPU: o nó tem várias GPUs e seria reservado inteiro. Com `--gpus-per-node=1`, a GPU já fica dedicada ao job.
- Memória da CPU: na `gpu-4-a100` o limite é 2000 MB por CPU. Para N = 32768 (~2,1 GB), peça `--cpus-per-task=2`.

## 5.7 Parâmetros

Os mesmos da versão sequencial: `N ITERACOES [BETA] [GAMA] [SEMENTE] [PREFIXO_SAIDA]`.

## 5.8 Verificação e obtenção dos resultados

- No `.out`: a GPU listada pelo `nvidia-smi`, os testes com `[PASSOU]` e as linhas "Estado final" e "Tempo da simulação".
- A linha "Estado final" deve ser idêntica à da versão sequencial para a mesma entrada.
- Registre dois tempos: o do laço (comparável às outras versões) e o total com as transferências.
- Copie os resultados com `scp` (docs/01, seção 1.9).

## 5.9 Experimentos específicos desta versão

- N de 1024 a 16384 (ou mais, enquanto couber na memória): a GPU tende a compensar só em grades grandes.
- Tempo do laço × tempo de transferência CPU↔GPU.
- Impacto da transferência: rode uma variante **sem** o `target enter/exit data`. Nela, cada iteração copia as duas grades para a GPU e de volta, e a diferença de tempo mostra o custo de mover os dados.
- Comparação com a versão CUDA na mesma GPU e com os mesmos N (docs/06 e docs/07).
